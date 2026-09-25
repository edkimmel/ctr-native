#include "platform/native_arcade_link_autopilot.h"

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_menu_input.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define ARGC(args) ((int)(sizeof(args) / sizeof((args)[0])))

#define DIGEST_TOTAL (NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT * NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES)

/* ApplyArgs must fail and leave a sentinel-filled struct byte-identical. */
static int RejectsUntouched(int argc, char *argv[])
{
	struct NativeArcadeLinkAutopilotOptions options;
	struct NativeArcadeLinkAutopilotOptions snapshot;

	memset(&options, 0xA5, sizeof(options));
	snapshot = options;
	if (NativeArcadeLinkAutopilotOptions_ApplyArgs(argc, argv, &options))
	{
		return 0;
	}
	return memcmp(&options, &snapshot, sizeof(options)) == 0;
}

static int TestOptions(void)
{
	struct NativeArcadeLinkAutopilotOptions options;
	char longPath[NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES + 1u];
	char maxPath[NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES];

	NativeArcadeLinkAutopilotOptions_SetDefaults(NULL);
	memset(&options, 0xA5, sizeof(options));
	NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
	CHECK(options.enabled == 0u);
	CHECK(options.reportPath[0] == '\0');
	CHECK(options.reportPath[NATIVE_ARCADE_LINK_AUTOPILOT_PATH_BYTES - 1u] == '\0');

	/* No option: success, nothing changes. */
	{
		char *argv[] = { "ctr_native", "--arcade-link", "cab1", "--arcade-link-port", "7001" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 0u);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(1, argv, &options) == 1);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(0, NULL, &options) == 1);
		CHECK(options.enabled == 0u);
	}
	/* The option among others; other arcade-link options are ignored. */
	{
		char *argv[] = { "ctr_native", "--arcade-link", "cab2", "--arcade-link-autopilot", "C:\\out\\cab2.report.txt", "--arcade-link-port",
			"7002" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 1u);
		CHECK(strcmp(options.reportPath, "C:\\out\\cab2.report.txt") == 0);
	}
	/* The longest path that fits. */
	{
		char *argv[] = { "ctr_native", "--arcade-link-autopilot", maxPath };

		memset(maxPath, 'r', sizeof(maxPath));
		maxPath[sizeof(maxPath) - 1u] = '\0';
		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(strcmp(options.reportPath, maxPath) == 0);
	}
	/* Errors leave the options untouched. */
	{
		char *missing[] = { "ctr_native", "--arcade-link-autopilot" };
		char *dash[] = { "ctr_native", "--arcade-link-autopilot", "--arcade-link", "cab1" };
		char *nullValue[] = { "ctr_native", "--arcade-link-autopilot", NULL };
		char *empty[] = { "ctr_native", "--arcade-link-autopilot", "" };
		char *repeated[] = { "ctr_native", "--arcade-link-autopilot", "a.txt", "--arcade-link-autopilot", "b.txt" };
		char *repeatedSame[] = { "ctr_native", "--arcade-link-autopilot", "a.txt", "--arcade-link-autopilot", "a.txt" };
		char *tooLong[] = { "ctr_native", "--arcade-link-autopilot", longPath };

		memset(longPath, 'r', sizeof(longPath));
		longPath[sizeof(longPath) - 1u] = '\0';
		CHECK(RejectsUntouched(ARGC(missing), missing));
		CHECK(RejectsUntouched(ARGC(dash), dash));
		CHECK(RejectsUntouched(ARGC(nullValue), nullValue));
		CHECK(RejectsUntouched(ARGC(empty), empty));
		CHECK(RejectsUntouched(ARGC(repeated), repeated));
		CHECK(RejectsUntouched(ARGC(repeatedSame), repeatedSame));
		CHECK(RejectsUntouched(ARGC(tooLong), tooLong));
		CHECK(RejectsUntouched(-1, missing));
		CHECK(RejectsUntouched(2, NULL));
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(missing), missing, NULL) == 0);
	}
	/* A NULL entry that is not the option's value is skipped. */
	{
		char *argv[] = { "ctr_native", NULL, "--arcade-link-autopilot", "r.txt" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 1u);
		CHECK(strcmp(options.reportPath, "r.txt") == 0);
	}
	/* The name is matched exactly. */
	{
		char *argv[] = { "ctr_native", "--arcade-link-autopilot=r.txt", "--arcade-link-autopilots", "r.txt" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 0u);
		CHECK(options.raceTickLimit == 0u);
	}
	return 0;
}

/* --arcade-link-autopilot-race-ticks (docs/LOCKSTEP_RACE_MILESTONE.md LR-60). */
static int TestRaceTicksOption(void)
{
	struct NativeArcadeLinkAutopilotOptions options;
	char *valid[] = { "1", "300", "18000", "00300" };
	const uint32_t validValues[] = { 1u, 300u, 18000u, 300u };
	uint32_t i;

	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_RACE_TICKS_MAX == 18000u);
	/* Absent: 0, the default bound. */
	memset(&options, 0xA5, sizeof(options));
	NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
	CHECK(options.raceTickLimit == 0u);
	{
		char *argv[] = { "ctr_native", "--arcade-link", "cab1", "--arcade-link-autopilot", "r.txt" };

		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 1u && options.raceTickLimit == 0u);
	}
	/* Valid values, before and after the autopilot option. */
	for (i = 0u; i < (uint32_t)ARGC(valid); i++)
	{
		char *after[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", valid[i] };
		char *before[] = { "ctr_native", "--arcade-link-autopilot-race-ticks", valid[i], "--arcade-link", "cab2", "--arcade-link-autopilot",
			"r.txt" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(after), after, &options) == 1);
		CHECK(options.enabled == 1u && options.raceTickLimit == validValues[i]);
		CHECK(strcmp(options.reportPath, "r.txt") == 0);
		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(before), before, &options) == 1);
		CHECK(options.enabled == 1u && options.raceTickLimit == validValues[i]);
		CHECK(strcmp(options.reportPath, "r.txt") == 0);
	}
	/* Errors leave the options untouched. */
	{
		char *zero[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "0" };
		char *above[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "18001" };
		char *sixDigits[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "000300" };
		char *huge[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "4294967296" };
		char *word[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "short" };
		char *negative[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "-5" };
		char *plus[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "+5" };
		char *hex[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "0x12c" };
		char *empty[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "" };
		char *junk[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "300x" };
		char *space[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "300 " };
		char *missing[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks" };
		char *nullValue[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", NULL };
		char *option[] = { "ctr_native", "--arcade-link-autopilot-race-ticks", "--arcade-link-autopilot", "r.txt" };
		char *twice[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks", "300",
			"--arcade-link-autopilot-race-ticks", "300" };
		char *twiceOther[] = { "ctr_native", "--arcade-link-autopilot-race-ticks", "300", "--arcade-link-autopilot", "r.txt",
			"--arcade-link-autopilot-race-ticks", "5" };
		char *alone[] = { "ctr_native", "--arcade-link", "cab1", "--arcade-link-autopilot-race-ticks", "300" };
		char *aloneValue[] = { "ctr_native", "--arcade-link-autopilot-race-ticks", "300" };

		CHECK(RejectsUntouched(ARGC(zero), zero));
		CHECK(RejectsUntouched(ARGC(above), above));
		CHECK(RejectsUntouched(ARGC(sixDigits), sixDigits));
		CHECK(RejectsUntouched(ARGC(huge), huge));
		CHECK(RejectsUntouched(ARGC(word), word));
		CHECK(RejectsUntouched(ARGC(negative), negative));
		CHECK(RejectsUntouched(ARGC(plus), plus));
		CHECK(RejectsUntouched(ARGC(hex), hex));
		CHECK(RejectsUntouched(ARGC(empty), empty));
		CHECK(RejectsUntouched(ARGC(junk), junk));
		CHECK(RejectsUntouched(ARGC(space), space));
		CHECK(RejectsUntouched(ARGC(missing), missing));
		CHECK(RejectsUntouched(ARGC(nullValue), nullValue));
		CHECK(RejectsUntouched(ARGC(option), option));
		CHECK(RejectsUntouched(ARGC(twice), twice));
		CHECK(RejectsUntouched(ARGC(twiceOther), twiceOther));
		CHECK(RejectsUntouched(ARGC(alone), alone));
		CHECK(RejectsUntouched(ARGC(aloneValue), aloneValue));
	}
	/* The name is matched exactly: near names are other parsers' (ignored). */
	{
		char *argv[] = { "ctr_native", "--arcade-link-autopilot", "r.txt", "--arcade-link-autopilot-race-ticks=5",
			"--arcade-link-autopilot-race-tick", "5" };

		NativeArcadeLinkAutopilotOptions_SetDefaults(&options);
		CHECK(NativeArcadeLinkAutopilotOptions_ApplyArgs(ARGC(argv), argv, &options) == 1);
		CHECK(options.enabled == 1u && options.raceTickLimit == 0u);
	}
	return 0;
}

static void View(struct NativeArcadeLinkHostView *view, uint32_t screen)
{
	memset(view, 0, sizeof(*view));
	view->screen = screen;
	view->localCab = 1u;
	view->attract = (uint8_t)((screen == NATIVE_ARCADE_FLOW_SCREEN_OFF) ? 1u : 0u);
}

static void SelectView(struct NativeArcadeLinkHostView *view, uint8_t status, uint8_t currentItem)
{
	View(view, NATIVE_ARCADE_FLOW_SCREEN_SELECT);
	view->select.active = 1u;
	view->select.humanCount = 2u;
	view->select.localHuman = 0u;
	view->select.status = status;
	view->select.currentItem = currentItem;
	view->select.humans[0].present = 1u;
	view->select.humans[0].characterID = 0u;
	view->select.humans[0].currentItem = currentItem;
	view->select.humans[1].present = 1u;
	view->select.humans[1].characterID = 1u;
}

static void ResultsView(struct NativeArcadeLinkHostView *view, uint32_t endReason, uint8_t rowsEnabled, uint32_t selectedRow)
{
	View(view, NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	view->endReason = endReason;
	view->rowsEnabled = rowsEnabled;
	view->selectedRow = selectedRow;
}

static void Match(struct NativeArcadeLinkHostMatch *match, uint32_t trackID, uint64_t seed)
{
	static const uint8_t roles[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS] = { 1u, 2u, 3u, 3u, 3u, 3u, 0u, 0u };
	static const uint8_t characters[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS] = { 0u, 1u, 6u, 4u, 2u, 3u, 0u, 0u };

	memset(match, 0, sizeof(*match));
	match->trackID = trackID;
	match->lapCount = 3u;
	match->masterSeed = seed;
	memcpy(match->slotRole, roles, sizeof(roles));
	memcpy(match->slotCharacter, characters, sizeof(characters));
}

static void Digests(uint8_t digests[DIGEST_TOTAL], uint8_t base)
{
	for (uint32_t i = 0; i < DIGEST_TOTAL; i++)
	{
		digests[i] = (uint8_t)(base + i);
	}
}

/* Decides with the given view: returns the held buttons, or 0xFFFFFFFF on a refusal. */
static uint32_t Held(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view)
{
	struct NativeArcadeLinkAutopilotOutput output;

	if (!NativeArcadeLinkAutopilot_Decide(autopilot, view, 0u, &output))
	{
		return 0xFFFFFFFFu;
	}
	return output.heldButtons;
}

static int TestDecideEnter(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkAutopilotOutput output;
	struct NativeArcadeLinkHostView view;

	NativeArcadeLinkAutopilot_Init(NULL);
	NativeArcadeLinkAutopilot_Init(&autopilot);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeLinkAutopilot_Decide(NULL, &view, 1u, &output) == 0);
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, NULL, 1u, &output) == 0);
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 1u, NULL) == 0);
	CHECK(autopilot.decisions == 0u);

	/* No enter outside the hook's enter window. */
	memset(&output, 0xA5, sizeof(output));
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 0u, &output) == 1);
	CHECK((output.enter == 0u) && (output.heldButtons == 0u) && (output.reserved[0] == 0u));
	/* Inside it, on any decision (no press cadence for the enter). */
	for (uint32_t i = 0; i < NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD; i++)
	{
		CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 1u, &output) == 1);
		CHECK((output.enter == 1u) && (output.heldButtons == 0u));
	}
	/* Once the session started (the view left OFF), never again. */
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY) == 0);
	CHECK(autopilot.sessionStarted == 1u);
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 1u, &output) == 1);
	CHECK((output.enter == 0u) && (output.heldButtons == 0u));
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 1u, &output) == 1);
	CHECK(output.enter == 0u);
	return 0;
}

static int TestDecideSelect(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkHostView view;

	/* CROSS on one decision in PRESS_PERIOD, released on the others, for
	 * every item while picking. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	for (uint8_t item = NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER; item < NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE; item++)
	{
		SelectView(&view, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING, item);
		for (uint32_t i = 0; i < 2u * NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD; i++)
		{
			const uint32_t expected = ((i % NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD) == 0u) ? NATIVE_ARCADE_MENU_BUTTON_CROSS : 0u;

			CHECK(Held(&autopilot, &view) == expected);
		}
	}
	/* A peer holds the local cursor's character: DOWN (next), not CROSS. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	SelectView(&view, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING, NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER);
	view.select.peerLockedCharacterMask = (uint16_t)(1u << 0);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_DOWN);
	/* The mask only matters for the local cursor's character and item. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	view.select.peerLockedCharacterMask = (uint16_t)(1u << 5);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_CROSS);
	NativeArcadeLinkAutopilot_Init(&autopilot);
	SelectView(&view, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING, NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_TRACK);
	view.select.peerLockedCharacterMask = (uint16_t)(1u << 0);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_CROSS);
	/* The local human is cab 2: its own entry decides. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	SelectView(&view, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING, NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER);
	view.select.localHuman = 1u;
	view.select.peerLockedCharacterMask = (uint16_t)(1u << 1);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_DOWN);

	/* Nothing once done picking, waiting, resolved, or inactive. */
	for (uint32_t c = 0; c < 4u; c++)
	{
		NativeArcadeLinkAutopilot_Init(&autopilot);
		SelectView(&view, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING, NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER);
		if (c == 0u)
		{
			view.select.currentItem = NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE;
		}
		else if (c == 1u)
		{
			view.select.status = NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_WAITING;
		}
		else if (c == 2u)
		{
			view.select.status = NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_RESOLVED;
		}
		else
		{
			view.select.active = 0u;
		}
		CHECK(Held(&autopilot, &view) == 0u);
	}
	/* Nothing on the screens that ignore menu input or wait. */
	{
		static const uint32_t quiet[] = { NATIVE_ARCADE_FLOW_SCREEN_LOBBY, NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND,
			NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT, NATIVE_ARCADE_FLOW_SCREEN_RACING, NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT,
			NATIVE_ARCADE_FLOW_SCREEN_EXIT, 99u };

		for (uint32_t i = 0; i < (uint32_t)(sizeof(quiet) / sizeof(quiet[0])); i++)
		{
			struct NativeArcadeLinkAutopilotOutput output;

			NativeArcadeLinkAutopilot_Init(&autopilot);
			View(&view, quiet[i]);
			CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &view, 1u, &output) == 1);
			CHECK((output.heldButtons == 0u) && (output.enter == 0u));
		}
	}
	return 0;
}

static int TestDecideResults(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkHostView view;

	/* Rows not enabled, or a race that did not finish: nothing. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 0u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	CHECK(Held(&autopilot, &view) == 0u);
	CHECK(autopilot.confirmedRow == 0u);
	NativeArcadeLinkAutopilot_Init(&autopilot);
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 1u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	CHECK(Held(&autopilot, &view) == 0u);

	/* Before the last race: REMATCH, the default row, is confirmed. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	autopilot.racesFinished = 1u;
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 1u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_CROSS);
	CHECK(autopilot.confirmedRow == NATIVE_ARCADE_FLOW_ROW_REMATCH + 1u);
	/* A released decision clears the confirmation. */
	CHECK(Held(&autopilot, &view) == 0u);
	CHECK(autopilot.confirmedRow == 0u);
	/* On EXIT it moves back to REMATCH (next wraps). */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	autopilot.racesFinished = 1u;
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 1u, NATIVE_ARCADE_FLOW_ROW_EXIT);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_DOWN);
	CHECK(autopilot.confirmedRow == 0u);

	/* After the last race: DOWN to EXIT, then CROSS. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	autopilot.racesFinished = NATIVE_ARCADE_LINK_AUTOPILOT_RACES;
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 1u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_DOWN);
	CHECK(autopilot.confirmedRow == 0u);
	for (uint32_t i = 1u; i < NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD; i++)
	{
		CHECK(Held(&autopilot, &view) == 0u);
	}
	view.selectedRow = NATIVE_ARCADE_FLOW_ROW_EXIT;
	CHECK(Held(&autopilot, &view) == NATIVE_ARCADE_MENU_BUTTON_CROSS);
	CHECK(autopilot.confirmedRow == NATIVE_ARCADE_FLOW_ROW_EXIT + 1u);
	return 0;
}

/* One owned frame: Decide with the view as the frame starts, then Observe
 * the tick's result. Returns Observe's value. */
static int Frame(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *before,
	const struct NativeArcadeLinkHostView *after, uint32_t action, struct NativeArcadeLinkAutopilotOutput *output)
{
	struct NativeArcadeLinkAutopilotOutput scratch;

	if (!NativeArcadeLinkAutopilot_Decide(autopilot, before, 0u, (output != NULL) ? output : &scratch))
	{
		return -1;
	}
	return NativeArcadeLinkAutopilot_Observe(autopilot, after, action);
}

/*
 * Drives one race from START_RACE to its RESULTS entry: the start with the
 * agreed match, the validation, the racing frames, and the finished RESULTS.
 */
static int RunRace(struct NativeArcadeLinkAutopilot *autopilot, uint32_t k, uint32_t launchNumber, uint64_t seed)
{
	struct NativeArcadeLinkHostView before;
	struct NativeArcadeLinkHostView after;
	struct NativeArcadeLinkHostMatch match;
	uint8_t digests[DIGEST_TOTAL];

	SelectView(&before, NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_CONFIRMED, NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE);
	before.screen = NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT;
	View(&after, NATIVE_ARCADE_FLOW_SCREEN_RACING);
	Match(&match, 3u + k, seed);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(autopilot, &match) == 1);
	CHECK(autopilot->racesStarted == k);
	CHECK(Frame(autopilot, &before, &after, NATIVE_ARCADE_FLOW_ACTION_START_RACE, NULL) == 0);
	Digests(digests, (uint8_t)(0x10u * k));
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(autopilot, k, launchNumber, digests) == 1);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(autopilot, k, launchNumber, digests) == 0);
	CHECK(autopilot->racesValidated == k);
	CHECK(Frame(autopilot, &after, &after, NATIVE_ARCADE_FLOW_ACTION_NONE, NULL) == 0);
	ResultsView(&before, NATIVE_ARCADE_FLOW_END_FINISHED, 0u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	CHECK(Frame(autopilot, &after, &before, NATIVE_ARCADE_FLOW_ACTION_NONE, NULL) == 0);
	CHECK(autopilot->racesFinished == k);
	return 0;
}

/* Presses through RESULTS until the confirming decision, then observes the tick moving to next. */
static int ConfirmResults(struct NativeArcadeLinkAutopilot *autopilot, uint32_t wantedRow, uint32_t nextScreen, uint32_t action)
{
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostView next;
	struct NativeArcadeLinkAutopilotOutput output;
	uint32_t row = NATIVE_ARCADE_FLOW_ROW_REMATCH;

	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 1u, row);
	for (uint32_t frame = 0; frame < 8u * NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD; frame++)
	{
		view.selectedRow = row;
		CHECK(NativeArcadeLinkAutopilot_Decide(autopilot, &view, 0u, &output) == 1);
		if (output.heldButtons == NATIVE_ARCADE_MENU_BUTTON_DOWN)
		{
			row = (row + 1u) % NATIVE_ARCADE_FLOW_RESULTS_ROW_COUNT;
		}
		else if (output.heldButtons == NATIVE_ARCADE_MENU_BUTTON_CROSS)
		{
			CHECK(row == wantedRow);
			View(&next, nextScreen);
			next.endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
			return (NativeArcadeLinkAutopilot_Observe(autopilot, &next, action) == 0) ? 0 : 1;
		}
		else
		{
			CHECK(output.heldButtons == 0u);
		}
		view.selectedRow = row;
		CHECK(NativeArcadeLinkAutopilot_Observe(autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	}
	return 1;
}

/* The whole run up to the EXIT screen, launch numbers as given. */
static int RunToExit(struct NativeArcadeLinkAutopilot *autopilot, uint32_t launch1, uint32_t launch2)
{
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkAutopilotOutput output;

	NativeArcadeLinkAutopilot_Init(autopilot);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeLinkAutopilot_Decide(autopilot, &view, 1u, &output) == 1);
	CHECK(output.enter == 1u);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	CHECK(NativeArcadeLinkAutopilot_Observe(autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_BEGIN_LOBBY) == 0);
	CHECK(RunRace(autopilot, 1u, launch1, UINT64_C(0x0123456789ABCDEF)) == 0);
	CHECK(ConfirmResults(autopilot, NATIVE_ARCADE_FLOW_ROW_REMATCH, NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT,
			  NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH) == 0);
	CHECK(autopilot->rematches == 1u);
	CHECK(RunRace(autopilot, 2u, launch2, UINT64_C(0xFEDCBA9876543210)) == 0);
	CHECK(ConfirmResults(autopilot, NATIVE_ARCADE_FLOW_ROW_EXIT, NATIVE_ARCADE_FLOW_SCREEN_EXIT, NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK) == 0);
	CHECK(autopilot->exitConfirmed == 1u);
	CHECK(autopilot->done == 0u);
	return 0;
}

/* A confirmation lasts one tick only, and only a confirmed row counts. */
static int TestConfirmations(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkHostView results;
	struct NativeArcadeLinkHostView rematchWait;
	struct NativeArcadeLinkHostView racing;

	ResultsView(&results, NATIVE_ARCADE_FLOW_END_FINISHED, 1u, NATIVE_ARCADE_FLOW_ROW_REMATCH);
	View(&rematchWait, NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT);
	View(&racing, NATIVE_ARCADE_FLOW_SCREEN_RACING);

	/* A race frame (Observe with no Decide) clears a pending confirmation. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &racing, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	autopilot.confirmedRow = (uint8_t)(NATIVE_ARCADE_FLOW_ROW_REMATCH + 1u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &racing, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	CHECK(autopilot.confirmedRow == 0u);
	CHECK((autopilot.rematches == 0u) && (autopilot.done == 0u));

	/* The confirming decision's tick changed nothing; the next tick has no
	 * Decide (a race or tick-only frame): Observe cleared the confirmation,
	 * so that tick's RESULTS -> REMATCH_WAIT change is not this autopilot's. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(RunRace(&autopilot, 1u, 1u, 7u) == 0);
	for (uint32_t i = 0; Held(&autopilot, &results) != NATIVE_ARCADE_MENU_BUTTON_CROSS; i++)
	{
		CHECK(i < NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD);
		CHECK(autopilot.confirmedRow == 0u);
		CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &results, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	}
	CHECK(autopilot.confirmedRow == NATIVE_ARCADE_FLOW_ROW_REMATCH + 1u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &results, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	CHECK(autopilot.confirmedRow == 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &rematchWait, NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH) == 0);
	CHECK(autopilot.rematches == 0u);
	CHECK(autopilot.done == 0u);

	/* A RESULTS -> REMATCH_WAIT change with no confirmation at all (the
	 * peer's rematch, or the RESULTS idle timeout) is not a rematch either. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(RunRace(&autopilot, 1u, 1u, 7u) == 0);
	CHECK(autopilot.lastScreen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(autopilot.confirmedRow == 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &rematchWait, NATIVE_ARCADE_FLOW_ACTION_BEGIN_REMATCH) == 0);
	CHECK(autopilot.rematches == 0u);
	CHECK(autopilot.done == 0u);
	return 0;
}

static int TestFullRun(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkAutopilotOutput output;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostView off;
	uint32_t ticks;

	/* Launch numbers need not be k (RL-S7 interpretation (d)). */
	CHECK(RunToExit(&autopilot, 1u, 3u) == 0);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	view.endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
	for (uint32_t i = 0; i < 10u; i++)
	{
		CHECK(Frame(&autopilot, &view, &view, NATIVE_ARCADE_FLOW_ACTION_NONE, &output) == 0);
		CHECK((output.heldButtons == 0u) && (output.enter == 0u));
	}
	ticks = autopilot.ticks;
	View(&off, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(Frame(&autopilot, &view, &off, NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE, NULL) == 1);
	CHECK(autopilot.done == 1u);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_PASS);
	CHECK(autopilot.ticks == ticks + 1u);
	CHECK((autopilot.racesStarted == 2u) && (autopilot.racesValidated == 2u) && (autopilot.racesFinished == 2u));
	CHECK(autopilot.races[0].launchNumber == 1u);
	CHECK(autopilot.races[1].launchNumber == 3u);
	CHECK(autopilot.races[1].match.masterSeed == UINT64_C(0xFEDCBA9876543210));
	CHECK(autopilot.races[1].digests[0][0] == 0x20u);

	/* Done: inert, never enters again, no more ticks. */
	CHECK(NativeArcadeLinkAutopilot_Decide(&autopilot, &off, 1u, &output) == 1);
	CHECK((output.heldButtons == 0u) && (output.enter == 0u));
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &off, NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) == 0);
	CHECK(autopilot.ticks == ticks + 1u);
	{
		struct NativeArcadeLinkHostMatch match;

		Match(&match, 3u, 1u);
		CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 0);
		CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_PASS);
	}
	return 0;
}

static int TestFailures(void)
{
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostMatch match;
	uint8_t digests[DIGEST_TOTAL];

	Match(&match, 3u, 1u);
	Digests(digests, 0u);

	/* A pre-race LINK_ERROR (select or launch) is RACE_FAILED. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 0u, 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK) == 1);
	CHECK((autopilot.done == 1u) && (autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED));
	CHECK(autopilot.lastScreen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS);
	CHECK(autopilot.lastEndReason == NATIVE_ARCADE_FLOW_END_LINK_ERROR);
	/* So is a race that ends in a peer timeout or desync. */
	for (uint32_t reason = NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT; reason <= NATIVE_ARCADE_FLOW_END_DESYNC; reason++)
	{
		NativeArcadeLinkAutopilot_Init(&autopilot);
		CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
		CHECK(NativeArcadeLinkAutopilot_RecordValidated(&autopilot, 1u, 1u, digests) == 1);
		View(&view, NATIVE_ARCADE_FLOW_SCREEN_RACING);
		CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_START_RACE) == 0);
		ResultsView(&view, reason, 0u, 0u);
		CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 1);
		CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED);
	}

	/* A FINISHED race that was never validated is EVIDENCE_MISSING. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_FINISHED, 0u, 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);
	/* So is a START_RACE without an agreed match, or a validation without digests. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, NULL) == 0);
	CHECK((autopilot.done == 1u) && (autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING));
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(&autopilot, 1u, 1u, NULL) == 0);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);

	/* A validation before any START_RACE, a jump of two, or a third race is UNEXPECTED_RACE. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(&autopilot, 0u, 1u, digests) == 0);
	CHECK(autopilot.done == 0u);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(&autopilot, 1u, 1u, digests) == 0);
	CHECK((autopilot.done == 1u) && (autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE));
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(&autopilot, 2u, 2u, digests) == 0);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE);
	NativeArcadeLinkAutopilot_Init(&autopilot);
	for (uint32_t i = 0; i < NATIVE_ARCADE_LINK_AUTOPILOT_RACES; i++)
	{
		CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	}
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 0);
	CHECK((autopilot.done == 1u) && (autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE));
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(NULL, &match) == 0);
	CHECK(NativeArcadeLinkAutopilot_RecordValidated(NULL, 1u, 1u, digests) == 0);

	/* The opponent left: EXIT with OPPONENT_LEFT is SESSION_LOST at once. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	view.endReason = NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT;
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST);

	/* Back to the title early (after one race) is SESSION_LOST. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	CHECK(RunRace(&autopilot, 1u, 1u, 7u) == 0);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST);

	/* Two races, but the RESULTS idle timeout took EXIT, not this autopilot:
	 * the EXIT change came on a tick without its confirmation. */
	CHECK(RunToExit(&autopilot, 1u, 2u) == 0);
	autopilot.exitConfirmed = 0u;
	autopilot.lastScreen = NATIVE_ARCADE_FLOW_SCREEN_RESULTS;
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_EXIT);
	view.endReason = NATIVE_ARCADE_FLOW_END_FINISHED;
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_CLOSE_LINK) == 0);
	CHECK(autopilot.exitConfirmed == 0u);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST);

	/* The deadline: TIMEOUT on the DEADLINE_TICKS-th observed tick. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_LOBBY);
	for (uint32_t i = 1u; i < NATIVE_ARCADE_LINK_AUTOPILOT_DEADLINE_TICKS; i++)
	{
		CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, (i % 2u) ? &view : NULL, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	}
	CHECK(autopilot.done == 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT);
	CHECK(autopilot.ticks == NATIVE_ARCADE_LINK_AUTOPILOT_DEADLINE_TICKS);
	CHECK(NativeArcadeLinkAutopilot_Observe(NULL, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 0);
	return 0;
}

static int TestNames(void)
{
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_PASS), "PASS") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT), "TIMEOUT") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED), "RACE_FAILED") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST), "SESSION_LOST") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE), "UNEXPECTED_RACE") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING), "EVIDENCE_MISSING") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_WRITE_FAILED), "REPORT_WRITE_FAILED") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ResultName(1u), "unknown") == 0);
	/* The result codes are frozen: the checker and the report name them. */
	CHECK((NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT == 40) && (NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_WRITE_FAILED == 45));
	CHECK(strcmp(NativeArcadeLinkAutopilot_ScreenName(NATIVE_ARCADE_FLOW_SCREEN_OFF), "OFF") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ScreenName(NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT), "SELECT_RESULT") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ScreenName(NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT), "REMATCH_WAIT") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_ScreenName(9u), "unknown") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_EndReasonName(NATIVE_ARCADE_FLOW_END_NONE), "NONE") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_EndReasonName(NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT), "OPPONENT_LEFT") == 0);
	CHECK(strcmp(NativeArcadeLinkAutopilot_EndReasonName(6u), "unknown") == 0);
	return 0;
}

/* sin and cos of x in [-pi, pi] by their Taylor series (no libm needed). */
static void SinCos(double x, double *s, double *c)
{
	double termS = x;
	double termC = 1.0;

	*s = 0.0;
	*c = 0.0;
	for (int n = 0; n < 30; n++)
	{
		*s += termS;
		*c += termC;
		termS *= -(x * x) / (double)((2 * n + 2) * (2 * n + 3));
		termC *= -(x * x) / (double)((2 * n + 1) * (2 * n + 2));
	}
}

static int32_t Round(double value)
{
	return (int32_t)((value >= 0.0) ? (value + 0.5) : (value - 0.5));
}

/* The unit vector of a 12-bit angle, scaled to 10000 (0 faces +z, 1024 +x). */
static void Direction(int32_t angle, int32_t *dx, int32_t *dz)
{
	const int32_t wrapped = angle & (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - 1);
	const int32_t centred = (wrapped > (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS / 2)) ? (wrapped - NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS)
	                                                                                   : wrapped;
	const double radians = ((double)centred * 6.283185307179586) / (double)NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS;
	double s;
	double c;

	SinCos(radians, &s, &c);
	*dx = Round(s * 10000.0);
	*dz = Round(c * 10000.0);
}

/* The circular distance of two 12-bit angles. */
static int32_t AngleDistance(int32_t a, int32_t b)
{
	int32_t d = (a - b) & (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - 1);

	return (d > (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS / 2)) ? (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - d) : d;
}

/* The steering buttons for a kart at the origin with heading, aiming along angle aim. */
static uint32_t SteerToward(int32_t heading, int32_t aim)
{
	struct NativeArcadeLinkAutopilotSteerFacts facts;

	memset(&facts, 0, sizeof(facts));
	facts.heading = heading;
	Direction(aim, &facts.aimX, &facts.aimZ);
	return NativeArcadeLinkAutopilot_Steer(&facts);
}

static int TestSteerAngle(void)
{
	const uint32_t cross = NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS;
	const uint32_t positive = cross | NATIVE_ARCADE_LINK_AUTOPILOT_STEER_POSITIVE;
	const uint32_t negative = cross | NATIVE_ARCADE_LINK_AUTOPILOT_STEER_NEGATIVE;

	/* The pad bits (PSX active-high), and LEFT raises the heading. */
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS == 0x4000u);
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_LEFT == 0x0080u);
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_RIGHT == 0x0020u);
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_STEER_POSITIVE == NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_LEFT);
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_STEER_NEGATIVE == NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_RIGHT);

	/* The angle: the axes, the diagonals, the origin, and the extremes. */
	CHECK(NativeArcadeLinkAutopilot_Angle(0, 0) == 0);
	CHECK(NativeArcadeLinkAutopilot_Angle(0, 100) == 0);
	CHECK(NativeArcadeLinkAutopilot_Angle(100, 0) == 1024);
	CHECK(NativeArcadeLinkAutopilot_Angle(0, -100) == 2048);
	CHECK(NativeArcadeLinkAutopilot_Angle(-100, 0) == 3072);
	CHECK(NativeArcadeLinkAutopilot_Angle(100, 100) == 512);
	CHECK(NativeArcadeLinkAutopilot_Angle(100, -100) == 1536);
	CHECK(NativeArcadeLinkAutopilot_Angle(-100, -100) == 2560);
	CHECK(NativeArcadeLinkAutopilot_Angle(-100, 100) == 3584);
	CHECK(NativeArcadeLinkAutopilot_Angle(INT32_MIN, 0) == 3072);
	CHECK(NativeArcadeLinkAutopilot_Angle(INT32_MAX, 0) == 1024);
	CHECK(NativeArcadeLinkAutopilot_Angle(0, INT32_MIN) == 2048);
	CHECK(NativeArcadeLinkAutopilot_Angle(INT32_MIN, INT32_MIN) == 2560);
	/* Every direction, within 3 units, always in range. */
	for (int32_t angle = 0; angle < NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS; angle++)
	{
		int32_t dx;
		int32_t dz;
		int32_t measured;

		Direction(angle, &dx, &dz);
		measured = NativeArcadeLinkAutopilot_Angle(dx, dz);
		CHECK((measured >= 0) && (measured < NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS));
		CHECK(AngleDistance(measured, angle) <= 3);
	}

	/* Straight, left (positive), and right (negative) of the heading. */
	CHECK(NativeArcadeLinkAutopilot_Steer(NULL) == 0u);
	CHECK(SteerToward(0, 0) == cross);
	CHECK(SteerToward(0, 512) == positive);
	CHECK(SteerToward(0, 3584) == negative);
	CHECK(SteerToward(1024, 1024) == cross);
	CHECK(SteerToward(1024, 1536) == positive);
	CHECK(SteerToward(1024, 512) == negative);
	/* The deadband: 40 units off goes straight, 60 steers. */
	CHECK(SteerToward(0, 40) == cross);
	CHECK(SteerToward(0, -40 & 4095) == cross);
	CHECK(SteerToward(0, 60) == positive);
	CHECK(SteerToward(0, 4036) == negative);
	/* Angle wrap: across 0 in both directions, and any heading value. */
	CHECK(SteerToward(4000, 100) == positive);
	CHECK(SteerToward(100, 4000) == negative);
	CHECK(SteerToward(-96, 100) == positive);
	CHECK(SteerToward(4000 + 4096, 100) == positive);
	CHECK(SteerToward(-96 - (4 * 4096), 4000) == cross);
	CHECK(SteerToward(4090, 10) == cross);
	/* Just behind: the error wraps to its most negative value. */
	CHECK(SteerToward(0, 2048) == negative);
	CHECK(SteerToward(0, 2000) == positive);
	CHECK(SteerToward(0, 2100) == negative);
	/* The aim point on the kart: straight. */
	{
		struct NativeArcadeLinkAutopilotSteerFacts facts;

		memset(&facts, 0, sizeof(facts));
		facts.kartX = -5000;
		facts.kartZ = 7000;
		facts.aimX = -5000;
		facts.aimZ = 7000;
		facts.heading = 1234;
		CHECK(NativeArcadeLinkAutopilot_Steer(&facts) == cross);
		/* Only differences matter. */
		facts.aimX = -4000;
		facts.aimZ = 8000;
		facts.heading = 0;
		CHECK(NativeArcadeLinkAutopilot_Steer(&facts) == positive);
		/* Extreme positions are clamped, not overflowed. */
		facts.kartX = INT32_MIN;
		facts.kartZ = 0;
		facts.aimX = INT32_MAX;
		facts.aimZ = 0;
		facts.heading = 1024;
		CHECK(NativeArcadeLinkAutopilot_Steer(&facts) == cross);
		facts.heading = 0;
		CHECK(NativeArcadeLinkAutopilot_Steer(&facts) == positive);
	}
	return 0;
}

static int Passed(int32_t kartX, int32_t kartZ, int32_t pointX, int32_t pointZ, int32_t previousX, int32_t previousZ)
{
	struct NativeArcadeLinkAutopilotPassFacts facts;

	facts.kartX = kartX;
	facts.kartZ = kartZ;
	facts.pointX = pointX;
	facts.pointZ = pointZ;
	facts.previousX = previousX;
	facts.previousZ = previousZ;
	return NativeArcadeLinkAutopilot_Passed(&facts);
}

static int TestPassed(void)
{
	CHECK(NATIVE_ARCADE_LINK_AUTOPILOT_PASS_RADIUS == 256);
	CHECK(NativeArcadeLinkAutopilot_Passed(NULL) == 0);
	/* Approach from -z to the point at the origin. */
	CHECK(Passed(0, -1000, 0, 0, 0, -1000) == 0);
	CHECK(Passed(0, -300, 0, 0, 0, -1000) == 0);
	CHECK(Passed(0, -257, 0, 0, 0, -1000) == 0);
	CHECK(Passed(0, -256, 0, 0, 0, -1000) == 1); /* on the radius */
	CHECK(Passed(0, -100, 0, 0, 0, -1000) == 1);
	CHECK(Passed(0, 0, 0, 0, 0, -1000) == 1);
	/* Wide of the point: past the line square to the approach, or not. */
	CHECK(Passed(2000, 10, 0, 0, 0, -1000) == 1);
	CHECK(Passed(2000, 0, 0, 0, 0, -1000) == 0);
	CHECK(Passed(-2000, -10, 0, 0, 0, -1000) == 0);
	CHECK(Passed(-2000, 1, 0, 0, 0, -1000) == 1);
	/* The same away from the origin, on a diagonal approach. */
	CHECK(Passed(5000 + 400, 5000 - 390, 5000, 5000, 4000, 4000) == 1);
	CHECK(Passed(5000 + 400, 5000 - 410, 5000, 5000, 4000, 4000) == 0);
	/* No approach (previous on the point): the radius alone. */
	CHECK(Passed(0, 300, 0, 0, 0, 0) == 0);
	CHECK(Passed(0, 200, 0, 0, 0, 0) == 1);
	/* Extreme positions are clamped, not overflowed. */
	CHECK(Passed(INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN) == 0);
	CHECK(Passed(INT32_MAX, INT32_MAX, 0, 0, INT32_MIN, INT32_MIN) == 1);
	CHECK(Passed(INT32_MIN, INT32_MIN, 0, 0, INT32_MIN, INT32_MIN) == 0);
	return 0;
}

static int TestReport(void)
{
	static const char expectedMatch[] = "agreed match track 4 laps 3 seed 0x0123456789ABCDEF slots 0 1 6 4 2 3 0 0 (12BBBB--)";
	struct NativeArcadeLinkAutopilot autopilot;
	struct NativeArcadeLinkHostView view;
	struct NativeArcadeLinkHostMatch match;
	char text[NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_BYTES];
	char expected[NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_BYTES];
	char digestHex[4][2u * NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES + 1u];
	size_t length = 0u;
	size_t expectedLength;

	/* The match text is the hook's agreed-match log text. */
	Match(&match, 4u, UINT64_C(0x0123456789ABCDEF));
	CHECK(NativeArcadeLinkAutopilot_FormatMatch(&match, text, sizeof(text), &length) == 1);
	CHECK(strcmp(text, expectedMatch) == 0);
	CHECK(length == strlen(expectedMatch));
	CHECK(NativeArcadeLinkAutopilot_FormatMatch(&match, text, strlen(expectedMatch), &length) == 0);
	CHECK(text[0] == '\0');
	CHECK(NativeArcadeLinkAutopilot_FormatMatch(NULL, text, sizeof(text), &length) == 0);

	/* A passed run: every line, in order. */
	CHECK(RunToExit(&autopilot, 1u, 2u) == 0);
	View(&view, NATIVE_ARCADE_FLOW_SCREEN_OFF);
	view.localCab = 2u;
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE) == 1);
	CHECK(autopilot.result == NATIVE_ARCADE_LINK_AUTOPILOT_PASS);
	for (uint32_t k = 0; k < 2u; k++)
	{
		for (uint32_t i = 0; i < NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES; i++)
		{
			(void)snprintf(&digestHex[2u * k][2u * i], 3u, "%02x", (unsigned)(uint8_t)(0x10u * (k + 1u) + i));
			(void)snprintf(&digestHex[2u * k + 1u][2u * i], 3u, "%02x",
				(unsigned)(uint8_t)(0x10u * (k + 1u) + NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES + i));
		}
	}
	CHECK(NativeArcadeLinkAutopilot_FormatReport(&autopilot, text, sizeof(text), &length) == 1);
	CHECK(length == strlen(text));
	/* Race k's digests are 0x10k + i over the 128 bytes; the first two of
	 * the four are checked in full, the last two by prefix below. */
	CHECK(strncmp(text,
		      "arcade link autopilot v1\ncab 2\nresult PASS (0)\nlast screen OFF end reason NONE\nticks ",
		      strlen("arcade link autopilot v1\ncab 2\nresult PASS (0)\nlast screen OFF end reason NONE\nticks ")) == 0);
	(void)snprintf(expected, sizeof(expected), "race 1 agreed match track 4 laps 3 seed 0x0123456789ABCDEF slots 0 1 6 4 2 3 0 0 (12BBBB--)\n"
		"race 1 validated launch 1 config %s plan %s bots ", digestHex[0], digestHex[1]);
	CHECK(strstr(text, expected) != NULL);
	(void)snprintf(expected, sizeof(expected), "race 2 agreed match track 5 laps 3 seed 0xFEDCBA9876543210 slots 0 1 6 4 2 3 0 0 (12BBBB--)\n"
		"race 2 validated launch 2 config %s plan %s bots ", digestHex[2], digestHex[3]);
	CHECK(strstr(text, expected) != NULL);
	expectedLength = strlen("end races 2\n");
	CHECK((length > expectedLength) && (strcmp(text + length - expectedLength, "end races 2\n") == 0));
	/* Exactly 5 header lines, 4 race lines, and the end line. */
	{
		uint32_t lines = 0u;

		for (size_t i = 0; i < length; i++)
		{
			lines += (text[i] == '\n') ? 1u : 0u;
		}
		CHECK(lines == 10u);
	}
	/* The race 1 validated line holds all four digests, each 64 hex digits. */
	{
		const char *line = strstr(text, "race 1 validated launch 1 config ");
		const char *end;

		CHECK(line != NULL);
		end = strchr(line, '\n');
		CHECK(end != NULL);
		CHECK((size_t)(end - line) == strlen("race 1 validated launch 1") + strlen(" config  plan  bots  bank ") + 4u * 64u);
	}
	/* Too small: nothing. */
	CHECK(NativeArcadeLinkAutopilot_FormatReport(&autopilot, text, 64u, &length) == 0);
	CHECK(text[0] == '\0');
	CHECK(NativeArcadeLinkAutopilot_FormatReport(NULL, text, sizeof(text), &length) == 0);

	/* A failed run reports its result and only the lines it recorded. */
	NativeArcadeLinkAutopilot_Init(&autopilot);
	Match(&match, 3u, 9u);
	CHECK(NativeArcadeLinkAutopilot_RecordMatch(&autopilot, &match) == 1);
	ResultsView(&view, NATIVE_ARCADE_FLOW_END_LINK_ERROR, 0u, 0u);
	CHECK(NativeArcadeLinkAutopilot_Observe(&autopilot, &view, NATIVE_ARCADE_FLOW_ACTION_NONE) == 1);
	CHECK(NativeArcadeLinkAutopilot_FormatReport(&autopilot, text, sizeof(text), &length) == 1);
	CHECK(strcmp(text, "arcade link autopilot v1\ncab 1\nresult RACE_FAILED (41)\nlast screen RESULTS end reason LINK_ERROR\nticks 1\n"
			   "race 1 agreed match track 3 laps 3 seed 0x0000000000000009 slots 0 1 6 4 2 3 0 0 (12BBBB--)\nend races 0\n") == 0);

	/* The writer: the formatted bytes, replaced on a second write. */
	{
		const char *path = "native_arcade_link_autopilot_test.report.txt";
		char readBack[NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_BYTES + 1u];
		size_t readLength;
		FILE *file;

		CHECK(NativeArcadeLinkAutopilot_WriteReport(path, &autopilot) == 1);
		CHECK(NativeArcadeLinkAutopilot_WriteReport(path, &autopilot) == 1);
		file = fopen(path, "rb");
		CHECK(file != NULL);
		readLength = fread(readBack, 1u, sizeof(readBack) - 1u, file);
		(void)fclose(file);
		(void)remove(path);
		readBack[readLength] = '\0';
		CHECK(strcmp(readBack, text) == 0);
		CHECK(NativeArcadeLinkAutopilot_WriteReport(NULL, &autopilot) == 0);
		CHECK(NativeArcadeLinkAutopilot_WriteReport("", &autopilot) == 0);
		CHECK(NativeArcadeLinkAutopilot_WriteReport(path, NULL) == 0);
	}
	return 0;
}

int main(void)
{
	CHECK(TestOptions() == 0);
	CHECK(TestRaceTicksOption() == 0);
	CHECK(TestDecideEnter() == 0);
	CHECK(TestDecideSelect() == 0);
	CHECK(TestDecideResults() == 0);
	CHECK(TestConfirmations() == 0);
	CHECK(TestFullRun() == 0);
	CHECK(TestFailures() == 0);
	CHECK(TestNames() == 0);
	CHECK(TestReport() == 0);
	CHECK(TestSteerAngle() == 0);
	CHECK(TestPassed() == 0);
	puts("native_arcade_link_autopilot_test: ok");
	return 0;
}
