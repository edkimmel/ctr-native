#include "MAIN/MainArcadeLinkLayout.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "fail %d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SC_OFF NATIVE_ARCADE_FLOW_SCREEN_OFF
#define SC_LOBBY NATIVE_ARCADE_FLOW_SCREEN_LOBBY
#define SC_MATCH_FOUND NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND
#define SC_RACING NATIVE_ARCADE_FLOW_SCREEN_RACING
#define SC_RESULTS NATIVE_ARCADE_FLOW_SCREEN_RESULTS
#define SC_REMATCH_WAIT NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT
#define SC_EXIT NATIVE_ARCADE_FLOW_SCREEN_EXIT

#define LS_WAITING NATIVE_ARCADE_FLOW_LOBBY_WAITING
#define LS_CONNECTING NATIVE_ARCADE_FLOW_LOBBY_CONNECTING
#define LS_READY NATIVE_ARCADE_FLOW_LOBBY_READY
#define LS_REJECTED NATIVE_ARCADE_FLOW_LOBBY_REJECTED
#define LS_LOST NATIVE_ARCADE_FLOW_LOBBY_LOST

#define END_NONE NATIVE_ARCADE_FLOW_END_NONE
#define END_FINISHED NATIVE_ARCADE_FLOW_END_FINISHED
#define END_PEER_TIMEOUT NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT
#define END_DESYNC NATIVE_ARCADE_FLOW_END_DESYNC
#define END_LINK_ERROR NATIVE_ARCADE_FLOW_END_LINK_ERROR
#define END_OPPONENT_LEFT NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT

#define ROW_REMATCH NATIVE_ARCADE_FLOW_ROW_REMATCH
#define ROW_EXIT NATIVE_ARCADE_FLOW_ROW_EXIT

#define BIG MAIN_ARCADE_LINK_FONT_BIG
#define SMALL MAIN_ARCADE_LINK_FONT_SMALL
#define ORANGE MAIN_ARCADE_LINK_COLOR_ORANGE
#define RED MAIN_ARCADE_LINK_COLOR_RED
#define WHITE MAIN_ARCADE_LINK_COLOR_WHITE
#define GRAY MAIN_ARCADE_LINK_COLOR_GRAY

struct ExpectedItem
{
	uint32_t kind;
	uint32_t font;
	uint32_t flags;
	int x, y, w, h;
	const char *text;
};

#define TXT(font, color, y, text) \
	{MAIN_ARCADE_LINK_ITEM_TEXT, (font), (color) | MAIN_ARCADE_LINK_JUSTIFY_CENTER, 256, (y), 0, 0, (text)}
#define HIGHLIGHT(y) {MAIN_ARCADE_LINK_ITEM_HIGHLIGHT, 0u, 0u, 136, (y), 240, 21, NULL}
#define PANEL {MAIN_ARCADE_LINK_ITEM_PANEL, 0u, 0u, 56, 28, 400, 176, NULL}
#define COUNT(array) ((uint32_t)(sizeof(array) / sizeof((array)[0])))

static int AllZero(const void *bytes, size_t size)
{
	const unsigned char *p = (const unsigned char *)bytes;
	for (size_t i = 0; i < size; i++)
		if (p[i] != 0u) return 0;
	return 1;
}

/* Returns 1 when the layout holds exactly the expected items, with every
 * unused byte and every unused item zero. */
static int Matches(const struct MainArcadeLinkLayout *layout, const struct ExpectedItem *expected, uint32_t count,
	int line)
{
	if (layout->count != count)
	{
		fprintf(stderr, "line %d: count %u, expected %u\n", line, (unsigned)layout->count, (unsigned)count);
		return 0;
	}
	for (uint32_t i = 0; i < count; i++)
	{
		const struct MainArcadeLinkItem *item = &layout->items[i];
		const struct ExpectedItem *want = &expected[i];
		if ((item->kind != want->kind) || (item->font != want->font) || (item->flags != want->flags) ||
			(item->x != want->x) || (item->y != want->y) || (item->w != want->w) || (item->h != want->h))
		{
			fprintf(stderr, "line %d: item %u fields (kind %u font %u flags 0x%x x %d y %d w %d h %d)\n", line,
				(unsigned)i, (unsigned)item->kind, (unsigned)item->font, (unsigned)item->flags, item->x, item->y,
				item->w, item->h);
			return 0;
		}
		if (want->text == NULL)
		{
			if (!AllZero(item->text, sizeof(item->text)))
			{
				fprintf(stderr, "line %d: item %u rectangle has text bytes\n", line, (unsigned)i);
				return 0;
			}
		}
		else
		{
			size_t length = strlen(want->text);
			if ((length >= sizeof(item->text)) || (memcmp(item->text, want->text, length + 1u) != 0) ||
				!AllZero(item->text + length, sizeof(item->text) - length))
			{
				fprintf(stderr, "line %d: item %u text '%.40s', expected '%s'\n", line, (unsigned)i, item->text,
					want->text);
				return 0;
			}
		}
	}
	for (uint32_t i = count; i < MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS; i++)
	{
		if (!AllZero(&layout->items[i], sizeof(layout->items[i])))
		{
			fprintf(stderr, "line %d: unused item %u is not zero\n", line, (unsigned)i);
			return 0;
		}
	}
	return 1;
}

static struct MainArcadeLinkLayoutInput MakeInput(uint32_t screen, uint32_t lobbyStatus, uint32_t endReason,
	uint32_t selectedRow, uint32_t ticks, uint8_t localCab, uint8_t rowsEnabled, uint8_t attract)
{
	struct MainArcadeLinkLayoutInput input;
	memset(&input, 0, sizeof(input));
	input.screen = screen;
	input.lobbyStatus = lobbyStatus;
	input.endReason = endReason;
	input.selectedRow = selectedRow;
	input.ticksInScreen = ticks;
	input.localCab = localCab;
	input.rowsEnabled = rowsEnabled;
	input.attract = attract;
	return input;
}

/* Builds into a garbage-filled layout; returns the Build result. */
static int BuildInto(const struct MainArcadeLinkLayoutInput *input, struct MainArcadeLinkLayout *layout)
{
	memset(layout, 0xa5, sizeof(*layout));
	return MainArcadeLinkLayout_Build(input, layout);
}

#define EXPECT_LAYOUT(input_value, expected_array) \
	do { \
		struct MainArcadeLinkLayoutInput input_ = (input_value); \
		struct MainArcadeLinkLayout layout_; \
		CHECK(BuildInto(&input_, &layout_) == 1); \
		CHECK(Matches(&layout_, (expected_array), COUNT(expected_array), __LINE__)); \
	} while (0)

#define EXPECT_EMPTY(input_value) \
	do { \
		struct MainArcadeLinkLayoutInput input_ = (input_value); \
		struct MainArcadeLinkLayout layout_; \
		CHECK(BuildInto(&input_, &layout_) == 1); \
		CHECK(layout_.count == 0u); \
		CHECK(AllZero(&layout_, sizeof(layout_))); \
	} while (0)

#define EXPECT_INVALID(input_value) \
	do { \
		struct MainArcadeLinkLayoutInput input_ = (input_value); \
		struct MainArcadeLinkLayout layout_; \
		struct MainArcadeLinkLayout sentinel_; \
		memset(&sentinel_, 0xa5, sizeof(sentinel_)); \
		CHECK(BuildInto(&input_, &layout_) == 0); \
		CHECK(memcmp(&layout_, &sentinel_, sizeof(layout_)) == 0); \
	} while (0)

static int TestMirroredConstants(void)
{
	CHECK(MAIN_ARCADE_LINK_FONT_BIG == 1u);
	CHECK(MAIN_ARCADE_LINK_FONT_SMALL == 2u);
	CHECK(MAIN_ARCADE_LINK_COLOR_ORANGE == 0u);
	CHECK(MAIN_ARCADE_LINK_COLOR_RED == 3u);
	CHECK(MAIN_ARCADE_LINK_COLOR_WHITE == 4u);
	CHECK(MAIN_ARCADE_LINK_COLOR_GRAY == 23u);
	CHECK(MAIN_ARCADE_LINK_JUSTIFY_CENTER == 0x8000u);
	CHECK(MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS == 10u);
	CHECK(MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES == 40u);
	CHECK(MAIN_ARCADE_LINK_DOT_STEP_TICKS == 15u);
	CHECK(MAIN_ARCADE_LINK_ITEM_TEXT == 1);
	CHECK(MAIN_ARCADE_LINK_ITEM_HIGHLIGHT == 2);
	CHECK(MAIN_ARCADE_LINK_ITEM_PANEL == 3);
	return 0;
}

static int TestInvalidInputs(void)
{
	struct MainArcadeLinkLayoutInput valid = MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u);
	struct MainArcadeLinkLayout layout;
	struct MainArcadeLinkLayout sentinel;

	memset(&sentinel, 0xa5, sizeof(sentinel));
	memset(&layout, 0xa5, sizeof(layout));
	CHECK(MainArcadeLinkLayout_Build(NULL, &layout) == 0);
	CHECK(memcmp(&layout, &sentinel, sizeof(layout)) == 0);
	CHECK(MainArcadeLinkLayout_Build(&valid, NULL) == 0);
	CHECK(MainArcadeLinkLayout_Build(NULL, NULL) == 0);

	EXPECT_INVALID(MakeInput(SC_EXIT + 1u, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u));
	EXPECT_INVALID(MakeInput(0xffffffffu, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u));
	EXPECT_INVALID(MakeInput(SC_LOBBY, LS_LOST + 1u, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u));
	EXPECT_INVALID(MakeInput(SC_RESULTS, LS_WAITING, END_OPPONENT_LEFT + 1u, ROW_REMATCH, 0u, 1u, 1u, 0u));
	EXPECT_INVALID(MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 0u, 0u, 0u));
	EXPECT_INVALID(MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 3u, 0u, 0u));
	EXPECT_INVALID(MakeInput(SC_RESULTS, LS_WAITING, END_FINISHED, ROW_REMATCH, 0u, 1u, 2u, 0u));
	EXPECT_INVALID(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 2u));
	/* Invalid fields are rejected even on screens that draw nothing. */
	EXPECT_INVALID(MakeInput(SC_OFF, LS_LOST + 1u, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u));
	EXPECT_INVALID(MakeInput(SC_RACING, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 2u, 7u, 0u));
	/* attract 1 is valid only with screen OFF. */
	for (uint32_t screen = SC_LOBBY; screen <= SC_EXIT; screen++)
	{
		EXPECT_INVALID(MakeInput(screen, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 1u));
		EXPECT_INVALID(MakeInput(screen, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 2u, 1u, 1u));
	}
	/* attract 1 on OFF still rejects a bad cabinet. */
	EXPECT_INVALID(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 0u, 0u, 1u));
	return 0;
}

static int TestEmptyScreens(void)
{
	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	{
		for (uint32_t reason = END_NONE; reason <= END_OPPONENT_LEFT; reason++)
		{
			EXPECT_EMPTY(MakeInput(SC_OFF, status, reason, ROW_REMATCH, 0u, 1u, 0u, 0u));
			EXPECT_EMPTY(MakeInput(SC_OFF, status, reason, ROW_EXIT, 45u, 2u, 1u, 0u));
			EXPECT_EMPTY(MakeInput(SC_RACING, status, reason, ROW_REMATCH, 0u, 1u, 0u, 0u));
			EXPECT_EMPTY(MakeInput(SC_RACING, status, reason, ROW_EXIT, 45u, 2u, 1u, 0u));
		}
	}
	return 0;
}

static int TestLobby(void)
{
	const struct ExpectedItem waiting[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OPPONENT"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem waitingCab2[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OPPONENT..."),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 2"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem connecting[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "CONNECTING"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem connectingDots[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "CONNECTING."),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 2"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem ready[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "OPPONENT FOUND"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem rejected[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, RED, 90, "LINK REFUSED"),
		TXT(SMALL, RED, 110, "SETTINGS DO NOT MATCH"),
		TXT(SMALL, ORANGE, 186, "CROSS: RETRY  TRIANGLE: BACK"),
		PANEL,
	};

	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), waiting);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_LOST, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), waiting);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 45u, 2u, 0u, 0u), waitingCab2);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_LOST, END_NONE, ROW_REMATCH, 45u, 2u, 1u, 0u), waitingCab2);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_CONNECTING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), connecting);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_CONNECTING, END_NONE, ROW_REMATCH, 16u, 2u, 0u, 0u), connectingDots);
	/* READY and REJECTED lines never animate. */
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_READY, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), ready);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_READY, END_NONE, ROW_REMATCH, 45u, 1u, 0u, 0u), ready);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_REJECTED, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), rejected);
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_REJECTED, END_NONE, ROW_EXIT, 30u, 2u, 1u, 0u), rejected);
	/* The end reason and focused row do not change the lobby. */
	EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_WAITING, END_DESYNC, ROW_EXIT, 0u, 1u, 1u, 0u), waiting);
	return 0;
}

static int TestDots(void)
{
	static const uint32_t ticks[] = {0u, 14u, 15u, 29u, 30u, 45u, 59u, 60u, 75u};
	static const char *const lines[] = {"WAITING FOR OPPONENT", "WAITING FOR OPPONENT",
		"WAITING FOR OPPONENT.", "WAITING FOR OPPONENT.", "WAITING FOR OPPONENT..", "WAITING FOR OPPONENT...",
		"WAITING FOR OPPONENT...", "WAITING FOR OPPONENT", "WAITING FOR OPPONENT."};

	for (uint32_t i = 0; i < COUNT(ticks); i++)
	{
		const struct ExpectedItem lobby[] = {
			TXT(BIG, ORANGE, 40, "ARCADE LINK"),
			TXT(SMALL, WHITE, 90, lines[i]),
			TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
			TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
			PANEL,
		};
		const struct ExpectedItem rematch[] = {
			TXT(BIG, ORANGE, 40, "REMATCH"),
			TXT(SMALL, WHITE, 90, lines[i]),
			TXT(SMALL, ORANGE, 186, "TRIANGLE: CANCEL"),
			PANEL,
		};
		EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, ticks[i], 1u, 0u, 0u), lobby);
		EXPECT_LAYOUT(MakeInput(SC_REMATCH_WAIT, LS_WAITING, END_NONE, ROW_REMATCH, ticks[i], 1u, 0u, 0u), rematch);
	}
	/* The tick count wraps without trouble: 0xffffffff / 15 = 286331153, mod 4 = 1. */
	{
		const struct ExpectedItem wrapped[] = {
			TXT(BIG, ORANGE, 40, "ARCADE LINK"),
			TXT(SMALL, WHITE, 90, "CONNECTING."),
			TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
			TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
			PANEL,
		};
		EXPECT_LAYOUT(MakeInput(SC_LOBBY, LS_CONNECTING, END_NONE, ROW_REMATCH, 0xffffffffu, 1u, 0u, 0u), wrapped);
	}
	return 0;
}

static int TestMatchFound(void)
{
	const struct ExpectedItem found[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "OPPONENT FOUND"),
		TXT(SMALL, WHITE, 110, "GET READY"),
		PANEL,
	};
	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	{
		EXPECT_LAYOUT(MakeInput(SC_MATCH_FOUND, status, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u), found);
		EXPECT_LAYOUT(MakeInput(SC_MATCH_FOUND, status, END_FINISHED, ROW_EXIT, 44u, 2u, 1u, 0u), found);
	}
	return 0;
}

static int TestResults(void)
{
	static const uint32_t reasons[] = {
		END_NONE, END_FINISHED, END_PEER_TIMEOUT, END_DESYNC, END_LINK_ERROR, END_OPPONENT_LEFT};
	static const char *const titles[] = {"RESULTS", "RACE COMPLETE", "OPPONENT DISCONNECTED", "RACE OUT OF SYNC",
		"LINK ERROR", "RESULTS"};
	static const uint32_t titleColors[] = {ORANGE, ORANGE, RED, RED, RED, ORANGE};

	for (uint32_t i = 0; i < COUNT(reasons); i++)
	{
		const struct ExpectedItem disabled[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(BIG, GRAY, 120, "REMATCH"),
			TXT(BIG, GRAY, 145, "EXIT"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			PANEL,
		};
		const struct ExpectedItem focusRematch[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(BIG, ORANGE, 120, "REMATCH"),
			TXT(BIG, ORANGE, 145, "EXIT"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			HIGHLIGHT(117),
			PANEL,
		};
		const struct ExpectedItem focusExit[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(BIG, ORANGE, 120, "REMATCH"),
			TXT(BIG, ORANGE, 145, "EXIT"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			HIGHLIGHT(142),
			PANEL,
		};
		/* rowsEnabled 0: GRAY rows, no highlight, whatever the focus. */
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_WAITING, reasons[i], ROW_REMATCH, 0u, 1u, 0u, 0u), disabled);
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_READY, reasons[i], ROW_EXIT, 29u, 2u, 0u, 0u), disabled);
		/* rowsEnabled 1: ORANGE rows, highlight on the focused row. */
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_WAITING, reasons[i], ROW_REMATCH, 30u, 1u, 1u, 0u), focusRematch);
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_LOST, reasons[i], ROW_EXIT, 30u, 2u, 1u, 0u), focusExit);
		/* Any row other than ROW_EXIT means ROW_REMATCH. */
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_WAITING, reasons[i], 2u, 30u, 1u, 1u, 0u), focusRematch);
		EXPECT_LAYOUT(MakeInput(SC_RESULTS, LS_WAITING, reasons[i], 0xffffffffu, 30u, 1u, 1u, 0u), focusRematch);
	}
	return 0;
}

static int TestRematchWait(void)
{
	const struct ExpectedItem waiting[] = {
		TXT(BIG, ORANGE, 40, "REMATCH"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OPPONENT.."),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: CANCEL"),
		PANEL,
	};
	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	{
		EXPECT_LAYOUT(MakeInput(SC_REMATCH_WAIT, status, END_FINISHED, ROW_REMATCH, 30u, 1u, 0u, 0u), waiting);
		EXPECT_LAYOUT(MakeInput(SC_REMATCH_WAIT, status, END_NONE, ROW_EXIT, 44u, 2u, 1u, 0u), waiting);
	}
	return 0;
}

static int TestExit(void)
{
	const struct ExpectedItem thanks[] = {
		TXT(BIG, ORANGE, 40, "THANKS FOR PLAYING"),
		PANEL,
	};
	const struct ExpectedItem left[] = {
		TXT(BIG, RED, 40, "OPPONENT LEFT"),
		PANEL,
	};
	for (uint32_t reason = END_NONE; reason < END_OPPONENT_LEFT; reason++)
	{
		EXPECT_LAYOUT(MakeInput(SC_EXIT, LS_WAITING, reason, ROW_REMATCH, 0u, 1u, 0u, 0u), thanks);
		EXPECT_LAYOUT(MakeInput(SC_EXIT, LS_LOST, reason, ROW_EXIT, 45u, 2u, 1u, 0u), thanks);
	}
	EXPECT_LAYOUT(MakeInput(SC_EXIT, LS_WAITING, END_OPPONENT_LEFT, ROW_REMATCH, 0u, 1u, 0u, 0u), left);
	EXPECT_LAYOUT(MakeInput(SC_EXIT, LS_REJECTED, END_OPPONENT_LEFT, ROW_EXIT, 89u, 2u, 1u, 0u), left);
	return 0;
}

static int TestAttract(void)
{
	const struct ExpectedItem blinkOn[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "PRESS START"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		PANEL,
	};
	const struct ExpectedItem blinkOff[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		PANEL,
	};
	const struct ExpectedItem blinkOnCab2[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "PRESS START"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 2"),
		PANEL,
	};
	const struct ExpectedItem blinkOffCab2[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 2"),
		PANEL,
	};

	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 1u), blinkOn);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 14u, 1u, 0u, 1u), blinkOn);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 15u, 1u, 0u, 1u), blinkOff);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 30u, 1u, 0u, 1u), blinkOn);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 45u, 1u, 0u, 1u), blinkOff);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 2u, 0u, 1u), blinkOnCab2);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 15u, 2u, 0u, 1u), blinkOffCab2);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 30u, 2u, 0u, 1u), blinkOnCab2);
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 45u, 2u, 0u, 1u), blinkOffCab2);
	/* Lobby status, end reason, focus, and rowsEnabled do not change it. */
	EXPECT_LAYOUT(MakeInput(SC_OFF, LS_REJECTED, END_DESYNC, ROW_EXIT, 30u, 1u, 1u, 1u), blinkOn);
	return 0;
}

static int TestLongestStringsFit(void)
{
	CHECK(strlen("CROSS: RETRY  TRIANGLE: BACK") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	CHECK(strlen("WAITING FOR OPPONENT...") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	CHECK(strlen("OPPONENT DISCONNECTED") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	CHECK(strlen("THIS CABINET: CAB 1") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	return 0;
}

/* Sweeps every valid input shape: builds are memcmp-equal regardless of the
 * previous contents of *out, every text is NUL-terminated inside its buffer
 * with zero tail bytes, and the count never exceeds the capacity. */
static int TestDeterminismSweep(void)
{
	static const uint32_t ticks[] = {0u, 14u, 15u, 30u, 45u, 60u, 0xffffffffu};
	uint32_t built = 0;

	for (uint32_t screen = SC_OFF; screen <= SC_EXIT; screen++)
	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	for (uint32_t reason = END_NONE; reason <= END_OPPONENT_LEFT; reason++)
	for (uint32_t row = 0u; row < 3u; row++)
	for (uint32_t t = 0u; t < COUNT(ticks); t++)
	for (uint8_t cab = 1u; cab <= 2u; cab++)
	for (uint8_t enabled = 0u; enabled <= 1u; enabled++)
	for (uint8_t attract = 0u; attract <= 1u; attract++)
	{
		struct MainArcadeLinkLayoutInput input =
			MakeInput(screen, status, reason, row, ticks[t], cab, enabled, attract);
		struct MainArcadeLinkLayout first;
		struct MainArcadeLinkLayout second;
		int expectValid = (attract == 0u) || (screen == SC_OFF);

		memset(&first, 0x00, sizeof(first));
		memset(&second, 0xff, sizeof(second));
		CHECK(MainArcadeLinkLayout_Build(&input, &first) == expectValid);
		CHECK(MainArcadeLinkLayout_Build(&input, &second) == expectValid);
		if (!expectValid) continue;
		built++;
		CHECK(memcmp(&first, &second, sizeof(first)) == 0);
		CHECK(first.count <= MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS);
		for (uint32_t i = 0; i < first.count; i++)
		{
			const struct MainArcadeLinkItem *item = &first.items[i];
			if (item->kind == MAIN_ARCADE_LINK_ITEM_TEXT)
			{
				const char *end = (const char *)memchr(item->text, '\0', sizeof(item->text));
				size_t length;
				CHECK(end != NULL);
				length = (size_t)(end - item->text);
				CHECK(length > 0u);
				CHECK(AllZero(item->text + length, sizeof(item->text) - length));
				CHECK((item->flags & MAIN_ARCADE_LINK_JUSTIFY_CENTER) != 0u);
				CHECK(item->x == 256);
				for (size_t c = 0; c < length; c++)
					CHECK((item->text[c] >= ' ') && (item->text[c] <= 'Z'));
			}
		}
		/* Exactly one PANEL, always last, on every non-empty layout. */
		if (first.count > 0u)
		{
			CHECK(first.items[first.count - 1u].kind == MAIN_ARCADE_LINK_ITEM_PANEL);
			for (uint32_t i = 0; i + 1u < first.count; i++)
				CHECK(first.items[i].kind != MAIN_ARCADE_LINK_ITEM_PANEL);
		}
	}
	CHECK(built > 0u);
	return 0;
}

int main(void)
{
	if (TestMirroredConstants() != 0) return 1;
	if (TestInvalidInputs() != 0) return 1;
	if (TestEmptyScreens() != 0) return 1;
	if (TestLobby() != 0) return 1;
	if (TestDots() != 0) return 1;
	if (TestMatchFound() != 0) return 1;
	if (TestResults() != 0) return 1;
	if (TestRematchWait() != 0) return 1;
	if (TestExit() != 0) return 1;
	if (TestAttract() != 0) return 1;
	if (TestLongestStringsFit() != 0) return 1;
	if (TestDeterminismSweep() != 0) return 1;
	printf("main_arcade_link_layout_test: ok\n");
	return 0;
}
