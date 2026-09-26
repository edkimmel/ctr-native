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
	CHECK(MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS == 32u);
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
	CHECK(strlen("WAITING FOR OTHER CABINET...") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	CHECK(strlen("PRESS START TO RACE SOLO") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
	CHECK(strlen("OTHER CABINET IS READY") + 1u <= MAIN_ARCADE_LINK_LAYOUT_TEXT_BYTES);
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

/* ---- Match-select screens (docs/MATCH_SELECT_MILESTONE.md section 2.8) ---- */

#define SC_SELECT NATIVE_ARCADE_FLOW_SCREEN_SELECT
#define SC_SELECT_RESULT NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT

#define IT_CHARACTER MAIN_ARCADE_LINK_SELECT_ITEM_CHARACTER
#define IT_TRACK MAIN_ARCADE_LINK_SELECT_ITEM_TRACK
#define IT_LAPS MAIN_ARCADE_LINK_SELECT_ITEM_LAPS
#define IT_DONE MAIN_ARCADE_LINK_SELECT_ITEM_DONE
#define LK_CHARACTER MAIN_ARCADE_LINK_SELECT_LOCK_CHARACTER
#define LK_TRACK MAIN_ARCADE_LINK_SELECT_LOCK_TRACK
#define LK_LAPS MAIN_ARCADE_LINK_SELECT_LOCK_LAPS

#define BLUE MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE
#define PRED MAIN_ARCADE_LINK_COLOR_PLAYER_RED
#define GREEN MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN
#define YELLOW MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW

/* Level IDs (include/namespace_Level.h). */
#define LV_DINGO_CANYON 0u
#define LV_CRASH_COVE 3u
#define LV_TIGER_TEMPLE 4u
#define LV_PAPU_PYRAMID 5u
#define LV_ROO_TUBES 6u
#define LV_SEWER_SPEEDWAY 8u
#define LV_N_GIN_LABS 11u
#define LV_OXIDE_STATION 13u
#define LV_SLIDE_COLISEUM 16u
#define LV_TURBO_TRACK 17u

#define TXTAT(font, color, x, y, text) \
	{MAIN_ARCADE_LINK_ITEM_TEXT, (font), (color) | MAIN_ARCADE_LINK_JUSTIFY_CENTER, (x), (y), 0, 0, (text)}
#define HL(x, y, w, h) {MAIN_ARCADE_LINK_ITEM_HIGHLIGHT, 0u, 0u, (x), (y), (w), (h), NULL}
#define SELECT_PANEL {MAIN_ARCADE_LINK_ITEM_PANEL, 0u, 0u, 4, 28, 504, 176, NULL}

/* The selection rules' tables, hard-coded: the grids list every item in
 * this order (native_match_select_rules: base characters 0..7; the 16 base
 * multiplayer tracks in retail menu order; laps 3, 5, 7). */
static const uint8_t k_characterOrder[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const char *const k_characterNames[8] = {
	"CRASH", "CORTEX", "TINY", "COCO", "N. GIN", "DINGODILE", "POLAR", "PURA"};
static const uint8_t k_trackOrder[16] = {3, 6, 4, 14, 9, 2, 8, 0, 5, 1, 12, 10, 15, 7, 11, 16};
static const char *const k_trackNames[16] = {"CRASH COVE", "ROO'S TUBES", "TIGER TEMPLE", "COCO PARK",
	"MYSTERY CAVES", "BLIZZARD BLUFF", "SEWER SPEEDWAY", "DINGO CANYON", "PAPU'S PYRAMID", "DRAGON MINES",
	"POLAR PASS", "CORTEX CASTLE", "TINY ARENA", "HOT AIR SKYWAY", "N. GIN LABS", "SLIDE COLISEUM"};
static const uint8_t k_lapOrder[3] = {3, 5, 7};
static const char *const k_lapNames[3] = {"3 LAPS", "5 LAPS", "7 LAPS"};

/* Grid geometry in the 512 x 216 space: names centred in 252-wide cells
 * from x 4 (column 0) and 256 (column 1), filled column by column. */
static int CharX(uint32_t i) { return (i < 4u) ? 130 : 382; }
static int CharY(uint32_t i) { return 78 + (24 * (int)(i % 4u)); }
static int TrackX(uint32_t i) { return (i < 8u) ? 130 : 382; }
static int TrackY(uint32_t i) { return 76 + (13 * (int)(i % 8u)); }
static int LapY(uint32_t i) { return 86 + (25 * (int)i); }

/* A two-human select on screen SELECT with both humans on CHARACTER: the
 * local human (0) on CRASH, the opponent (1) on CORTEX, both on CRASH COVE
 * and 3 laps; 600 ticks left. */
static struct MainArcadeLinkLayoutInput MakeSelect(uint32_t screen)
{
	struct MainArcadeLinkLayoutInput input = MakeInput(screen, LS_READY, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u);
	struct MainArcadeLinkLayoutSelect *sel = &input.select;

	sel->active = 1u;
	sel->humanCount = 2u;
	sel->localHuman = 0u;
	sel->currentItem = IT_CHARACTER;
	sel->ticksLeft = 600u;
	for (uint32_t h = 0; h < 2u; h++)
	{
		sel->humans[h].present = 1u;
		sel->humans[h].characterID = (uint8_t)h;
		sel->humans[h].trackID = LV_CRASH_COVE;
		sel->humans[h].lapCount = 3u;
		sel->humans[h].currentItem = IT_CHARACTER;
	}
	return input;
}

/* Puts human h on item it with the matching lock mask (and the local
 * current item when h is local). */
static void SetItem(struct MainArcadeLinkLayoutInput *input, uint32_t h, uint32_t it)
{
	input->select.humans[h].currentItem = (uint8_t)it;
	input->select.humans[h].lockMask = (uint8_t)((1u << it) - 1u);
	if (h == input->select.localHuman) input->select.currentItem = (uint8_t)it;
}

/* A resolved two-human select on SELECT_RESULT: CRASH and CORTEX on
 * TIGER TEMPLE (drawn), 3 laps, bots POLAR, N. GIN, TINY, COCO (retail 2P
 * AI set 0, {6,4,2,3}). */
static struct MainArcadeLinkLayoutInput MakeResult(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT_RESULT);
	struct MainArcadeLinkLayoutSelect *sel = &input.select;

	SetItem(&input, 0u, IT_DONE);
	SetItem(&input, 1u, IT_DONE);
	sel->ticksLeft = 0u;
	sel->status = 3u;
	sel->resolved = 1u;
	sel->trackID = LV_TIGER_TEMPLE;
	sel->lapCount = 3u;
	sel->trackDrawn = 1u;
	sel->humanCharacter[0] = 0u;
	sel->humanCharacter[1] = 1u;
	sel->botCount = 4u;
	sel->botCharacter[0] = 6u;
	sel->botCharacter[1] = 4u;
	sel->botCharacter[2] = 2u;
	sel->botCharacter[3] = 3u;
	return input;
}

static int TestSelectConstants(void)
{
	CHECK(MAIN_ARCADE_LINK_COLOR_PLAYER_BLUE == 24u);
	CHECK(MAIN_ARCADE_LINK_COLOR_PLAYER_RED == 25u);
	CHECK(MAIN_ARCADE_LINK_COLOR_PLAYER_GREEN == 26u);
	CHECK(MAIN_ARCADE_LINK_COLOR_PLAYER_YELLOW == 27u);
	CHECK(MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS == 4u);
	CHECK(MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS == 8u);
	CHECK(IT_CHARACTER == 0u);
	CHECK(IT_TRACK == 1u);
	CHECK(IT_LAPS == 2u);
	CHECK(IT_DONE == 3u);
	CHECK(LK_CHARACTER == 1u);
	CHECK(LK_TRACK == 2u);
	CHECK(LK_LAPS == 4u);
	CHECK(MAIN_ARCADE_LINK_SELECT_STATUS_FAILED == 4u);
	CHECK(MAIN_ARCADE_LINK_SELECT_TICKS_PER_SECOND == 30u);
	CHECK(SC_SELECT == 7u);
	CHECK(SC_SELECT_RESULT == 8u);
	return 0;
}

/* The full character screen, both humans still choosing: P1 (local) on
 * CRASH, P2 on TINY. Every name in list order down the columns, then the
 * markers in human order, the footer, the highlight on CRASH, the panel. */
static int TestSelectCharacter(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
	const struct ExpectedItem expected[] = {
		TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
		TXT(SMALL, WHITE, 60, "TIME 20"),
		TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
		TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
		TXTAT(BIG, ORANGE, 130, 126, "TINY"),
		TXTAT(BIG, ORANGE, 130, 150, "COCO"),
		TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
		TXTAT(BIG, ORANGE, 382, 102, "DINGODILE"),
		TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
		TXTAT(BIG, ORANGE, 382, 150, "PURA"),
		TXTAT(BIG, BLUE, 25, 78, "P1"),
		TXTAT(BIG, PRED, 235, 126, "P2"),
		TXT(SMALL, PRED, 186, "P2: CHOOSING CHARACTER"),
		HL(8, 75, 244, 21),
		SELECT_PANEL,
	};

	input.select.humans[1].characterID = 2u;
	EXPECT_LAYOUT(input, expected);

	/* The list order is the rules' order, column by column. */
	for (uint32_t i = 0; i < 8u; i++)
	{
		CHECK(k_characterOrder[i] == i);
		CHECK(expected[2u + i].x == CharX(i));
		CHECK(expected[2u + i].y == CharY(i));
		CHECK(strcmp(expected[2u + i].text, k_characterNames[i]) == 0);
	}
	return 0;
}

/* Taken characters are GRAY; a peer's locked pick keeps its marker; the
 * footer follows the peer's progress; the highlight follows the local
 * cursor into column 1; two humans on one cell sit at their own offsets. */
static int TestSelectCharacterTakenAndCursor(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
	const struct ExpectedItem expected[] = {
		TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
		TXT(SMALL, WHITE, 60, "TIME 3"),
		TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
		TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
		TXTAT(BIG, ORANGE, 130, 126, "TINY"),
		TXTAT(BIG, ORANGE, 130, 150, "COCO"),
		TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
		TXTAT(BIG, GRAY, 382, 102, "DINGODILE"),
		TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
		TXTAT(BIG, ORANGE, 382, 150, "PURA"),
		TXTAT(BIG, BLUE, 277, 150, "P1"),
		TXTAT(BIG, PRED, 487, 102, "P2"),
		TXT(SMALL, PRED, 186, "P2: VOTING TRACK"),
		HL(260, 147, 244, 21),
		SELECT_PANEL,
	};
	const struct ExpectedItem same[] = {
		TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
		TXT(SMALL, WHITE, 60, "TIME 3"),
		TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
		TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
		TXTAT(BIG, ORANGE, 130, 126, "TINY"),
		TXTAT(BIG, ORANGE, 130, 150, "COCO"),
		TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
		TXTAT(BIG, GRAY, 382, 102, "DINGODILE"),
		TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
		TXTAT(BIG, ORANGE, 382, 150, "PURA"),
		TXTAT(BIG, BLUE, 277, 102, "P1"),
		TXTAT(BIG, PRED, 487, 102, "P2"),
		TXT(SMALL, PRED, 186, "P2: VOTING TRACK"),
		HL(260, 99, 244, 21),
		SELECT_PANEL,
	};

	input.select.ticksLeft = 61u;
	input.select.humans[0].characterID = 7u;
	input.select.humans[1].characterID = 5u;
	SetItem(&input, 1u, IT_TRACK);
	input.select.peerLockedCharacterMask = (uint16_t)(1u << 5);
	EXPECT_LAYOUT(input, expected);

	/* The local cursor on the taken character: still GRAY, highlighted, and
	 * both markers on the one cell at their own offsets. */
	input.select.humans[0].characterID = 5u;
	EXPECT_LAYOUT(input, same);
	return 0;
}

/* One human; the local human as P2 (cabinet 2); an opponent not yet heard;
 * an opponent still behind on an earlier item gets no marker. */
static int TestSelectMarkersOneAndTwo(void)
{
	{
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "VOTE LAPS"),
			TXT(SMALL, WHITE, 60, "TIME 1"),
			TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
			TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
			TXTAT(BIG, ORANGE, 256, 136, "7 LAPS"),
			TXTAT(BIG, BLUE, 151, 136, "P1"),
			HL(134, 133, 244, 21),
			SELECT_PANEL,
		};
		input.select.humanCount = 1u;
		memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		SetItem(&input, 0u, IT_LAPS);
		input.select.humans[0].lapCount = 7u;
		input.select.ticksLeft = 1u;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Cabinet 2: the local human is P2 (the highlight is on its cell,
		 * 5 LAPS); P1 votes on 3 LAPS with no highlight. */
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "VOTE LAPS"),
			TXT(SMALL, WHITE, 60, "TIME 20"),
			TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
			TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
			TXTAT(BIG, ORANGE, 256, 136, "7 LAPS"),
			TXTAT(BIG, BLUE, 151, 86, "P1"),
			TXTAT(BIG, PRED, 361, 111, "P2"),
			TXT(SMALL, BLUE, 186, "P1: VOTING LAPS"),
			HL(134, 108, 244, 21),
			SELECT_PANEL,
		};
		input.localCab = 2u;
		input.select.localHuman = 1u;
		SetItem(&input, 0u, IT_LAPS);
		SetItem(&input, 1u, IT_LAPS);
		input.select.humans[1].lapCount = 5u;
		input.select.ticksLeft = 599u;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* The opponent is not heard yet: CONNECTING, and no marker. */
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "VOTE LAPS"),
			TXT(SMALL, WHITE, 60, "TIME 20"),
			TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
			TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
			TXTAT(BIG, ORANGE, 256, 136, "7 LAPS"),
			TXTAT(BIG, BLUE, 151, 86, "P1"),
			TXT(SMALL, PRED, 186, "P2: CONNECTING"),
			HL(134, 83, 244, 21),
			SELECT_PANEL,
		};
		SetItem(&input, 0u, IT_LAPS);
		memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		EXPECT_LAYOUT(input, expected);
		/* Garbage in an absent human's fields is ignored. */
		input.select.humans[1].characterID = 0xeeu;
		input.select.humans[1].trackID = LV_OXIDE_STATION;
		input.select.humans[1].lapCount = 4u;
		input.select.humans[1].lockMask = 0xffu;
		input.select.humans[1].currentItem = 9u;
		EXPECT_LAYOUT(input, expected);
		/* So is every human at or above humanCount. */
		input.select.humans[3] = input.select.humans[1];
		input.select.humans[3].present = 7u;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* The opponent is still choosing a character: no marker on the lap
		 * screen, though its lap cursor exists. */
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "VOTE LAPS"),
			TXT(SMALL, WHITE, 60, "TIME 20"),
			TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
			TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
			TXTAT(BIG, ORANGE, 256, 136, "7 LAPS"),
			TXTAT(BIG, BLUE, 151, 86, "P1"),
			TXT(SMALL, PRED, 186, "P2: CHOOSING CHARACTER"),
			HL(134, 83, 244, 21),
			SELECT_PANEL,
		};
		SetItem(&input, 0u, IT_LAPS);
		EXPECT_LAYOUT(input, expected);
	}
	return 0;
}

/* The track screen: 16 FONT_SMALL names in the rules' order down two
 * columns, small markers, WHITE on a locked vote, the small highlight. */
static int TestSelectTrack(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
	struct ExpectedItem expected[16 + 7];
	uint32_t n = 0;

	SetItem(&input, 0u, IT_TRACK);
	SetItem(&input, 1u, IT_LAPS);
	input.select.humans[0].trackID = LV_SEWER_SPEEDWAY; /* index 6: the longest name, column 0 */
	input.select.humans[1].trackID = LV_SEWER_SPEEDWAY; /* a locked vote */
	input.select.ticksLeft = 31u;

	expected[n++] = (struct ExpectedItem)TXT(BIG, ORANGE, 40, "VOTE TRACK");
	expected[n++] = (struct ExpectedItem)TXT(SMALL, WHITE, 60, "TIME 2");
	for (uint32_t i = 0; i < 16u; i++)
	{
		expected[n++] = (struct ExpectedItem)TXTAT(
			SMALL, (i == 6u) ? WHITE : ORANGE, TrackX(i), TrackY(i), k_trackNames[i]);
	}
	expected[n++] = (struct ExpectedItem)TXTAT(SMALL, BLUE, 21, TrackY(6), "P1");
	expected[n++] = (struct ExpectedItem)TXTAT(SMALL, PRED, 239, TrackY(6), "P2");
	expected[n++] = (struct ExpectedItem)TXT(SMALL, PRED, 186, "P2: VOTING LAPS");
	expected[n++] = (struct ExpectedItem)HL(8, TrackY(6) - 2, 244, 12);
	expected[n++] = (struct ExpectedItem)SELECT_PANEL;
	{
		struct MainArcadeLinkLayout layout;
		CHECK(BuildInto(&input, &layout) == 1);
		CHECK(Matches(&layout, expected, n, __LINE__));
	}
	CHECK(TrackY(7) == 167);
	CHECK(TrackY(15) == 167);

	/* Every track's cursor lands on its own cell, in the rules' order. */
	for (uint32_t i = 0; i < 16u; i++)
	{
		const uint32_t next = (i + 1u) % 16u;
		struct MainArcadeLinkLayout layout;
		input.select.humans[0].trackID = k_trackOrder[i];
		input.select.humans[1].trackID = k_trackOrder[next];
		CHECK(BuildInto(&input, &layout) == 1);
		CHECK(layout.count == n);
		CHECK(strcmp(layout.items[2u + i].text, k_trackNames[i]) == 0);
		CHECK(layout.items[2u + next].flags == (WHITE | MAIN_ARCADE_LINK_JUSTIFY_CENTER));
		CHECK(layout.items[2u + i].flags == (ORANGE | MAIN_ARCADE_LINK_JUSTIFY_CENTER));
		CHECK(layout.items[18].x == ((i < 8u) ? 21 : 273));
		CHECK(layout.items[18].y == TrackY(i));
		CHECK(layout.items[19].x == ((next < 8u) ? 239 : 491));
		CHECK(layout.items[19].y == TrackY(next));
		CHECK(layout.items[21].kind == MAIN_ARCADE_LINK_ITEM_HIGHLIGHT);
		CHECK(layout.items[21].x == ((i < 8u) ? 8 : 260));
		CHECK(layout.items[21].y == TrackY(i) - 2);
	}
	return 0;
}

/* The lap screen: three FONT_BIG rows, both markers, a locked vote WHITE. */
static int TestSelectLaps(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
	const struct ExpectedItem expected[] = {
		TXT(BIG, ORANGE, 40, "VOTE LAPS"),
		TXT(SMALL, WHITE, 60, "TIME 0"),
		TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
		TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
		TXTAT(BIG, WHITE, 256, 136, "7 LAPS"),
		TXTAT(BIG, BLUE, 151, 111, "P1"),
		TXTAT(BIG, PRED, 361, 136, "P2"),
		TXT(SMALL, PRED, 186, "P2: READY"),
		HL(134, 108, 244, 21),
		SELECT_PANEL,
	};

	SetItem(&input, 0u, IT_LAPS);
	SetItem(&input, 1u, IT_DONE);
	input.select.humans[0].lapCount = 5u;
	input.select.humans[1].lapCount = 7u;
	input.select.ticksLeft = 0u;
	EXPECT_LAYOUT(input, expected);
	for (uint32_t i = 0; i < 3u; i++)
	{
		CHECK(expected[2u + i].y == LapY(i));
		CHECK(strcmp(expected[2u + i].text, k_lapNames[i]) == 0);
		CHECK(k_lapOrder[i] == (uint8_t)(3u + (2u * i)));
	}
	return 0;
}

/* Four humans: digit markers paired 1,3 left and 2,4 right, all four on one
 * cell without colliding; the footer shows three short entries 160 apart;
 * an absent human shows JOINING and no marker. Three humans on the track
 * screen use small digits and two footer entries. */
static int TestSelectFourHumans(void)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
	const struct ExpectedItem all[] = {
		TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
		TXT(SMALL, WHITE, 60, "TIME 20"),
		TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
		TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
		TXTAT(BIG, ORANGE, 130, 126, "TINY"),
		TXTAT(BIG, ORANGE, 130, 150, "COCO"),
		TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
		TXTAT(BIG, ORANGE, 382, 102, "DINGODILE"),
		TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
		TXTAT(BIG, ORANGE, 382, 150, "PURA"),
		TXTAT(BIG, BLUE, 268, 102, "1"),
		TXTAT(BIG, PRED, 478, 102, "2"),
		TXTAT(BIG, GREEN, 285, 102, "3"),
		TXTAT(BIG, YELLOW, 495, 102, "4"),
		TXTAT(SMALL, PRED, 96, 186, "P2 CHARACTER"),
		TXTAT(SMALL, GREEN, 256, 186, "P3 CHARACTER"),
		TXTAT(SMALL, YELLOW, 416, 186, "P4 CHARACTER"),
		HL(260, 99, 244, 21),
		SELECT_PANEL,
	};
	const struct ExpectedItem absent[] = {
		TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
		TXT(SMALL, WHITE, 60, "TIME 20"),
		TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
		TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
		TXTAT(BIG, ORANGE, 130, 126, "TINY"),
		TXTAT(BIG, ORANGE, 130, 150, "COCO"),
		TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
		TXTAT(BIG, ORANGE, 382, 102, "DINGODILE"),
		TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
		TXTAT(BIG, ORANGE, 382, 150, "PURA"),
		TXTAT(BIG, BLUE, 268, 102, "1"),
		TXTAT(BIG, PRED, 478, 102, "2"),
		TXTAT(BIG, GREEN, 285, 102, "3"),
		TXTAT(SMALL, PRED, 96, 186, "P2 CHARACTER"),
		TXTAT(SMALL, GREEN, 256, 186, "P3 CHARACTER"),
		TXTAT(SMALL, YELLOW, 416, 186, "P4 JOINING"),
		HL(260, 99, 244, 21),
		SELECT_PANEL,
	};

	input.select.humanCount = 4u;
	for (uint32_t h = 0; h < 4u; h++)
	{
		input.select.humans[h] = input.select.humans[0];
		input.select.humans[h].characterID = 5u;
	}
	EXPECT_LAYOUT(input, all);
	memset(&input.select.humans[3], 0, sizeof(input.select.humans[3]));
	EXPECT_LAYOUT(input, absent);

	/* Column 0 and the lap column use the same slots. */
	{
		struct MainArcadeLinkLayout layout;
		for (uint32_t h = 0; h < 4u; h++)
		{
			input.select.humans[h] = input.select.humans[0];
			input.select.humans[h].characterID = 0u;
		}
		CHECK(BuildInto(&input, &layout) == 1);
		CHECK((layout.items[10].x == 16) && (layout.items[11].x == 226));
		CHECK((layout.items[12].x == 33) && (layout.items[13].x == 243));
		CHECK(layout.items[13].y == 78);
		for (uint32_t h = 0; h < 4u; h++) SetItem(&input, h, IT_LAPS);
		CHECK(BuildInto(&input, &layout) == 1);
		CHECK((layout.items[5].x == 142) && (layout.items[6].x == 352));
		CHECK((layout.items[7].x == 159) && (layout.items[8].x == 369));
		CHECK((layout.items[8].y == 86) && (strcmp(layout.items[8].text, "4") == 0));
		CHECK(strcmp(layout.items[11].text, "P4 LAPS") == 0);
	}

	/* Three humans on the track screen, local P3: small digits, two footer
	 * entries, the highlight on P3's cell. */
	{
		struct MainArcadeLinkLayoutInput three = MakeSelect(SC_SELECT);
		struct MainArcadeLinkLayout layout;

		three.select.humanCount = 3u;
		three.select.localHuman = 2u;
		three.select.humans[2] = three.select.humans[0];
		three.select.humans[2].characterID = 2u;
		SetItem(&three, 0u, IT_TRACK);
		SetItem(&three, 1u, IT_DONE);
		SetItem(&three, 2u, IT_TRACK);
		three.select.humans[0].trackID = LV_SLIDE_COLISEUM; /* index 15 */
		three.select.humans[1].trackID = LV_SLIDE_COLISEUM;
		three.select.humans[2].trackID = LV_SLIDE_COLISEUM;
		CHECK(BuildInto(&three, &layout) == 1);
		CHECK(layout.count == 2u + 16u + 3u + 2u + 2u);
		CHECK(layout.items[2u + 15u].flags == (WHITE | MAIN_ARCADE_LINK_JUSTIFY_CENTER));
		CHECK((layout.items[18].x == 266) && (layout.items[18].y == 167) && (strcmp(layout.items[18].text, "1") == 0));
		CHECK((layout.items[19].x == 484) && (strcmp(layout.items[19].text, "2") == 0));
		CHECK((layout.items[20].x == 279) && (strcmp(layout.items[20].text, "3") == 0));
		CHECK(layout.items[20].font == SMALL);
		CHECK((layout.items[21].x == 176) && (layout.items[21].y == 186) && (strcmp(layout.items[21].text, "P1 TRACK") == 0));
		CHECK(layout.items[21].flags == (BLUE | MAIN_ARCADE_LINK_JUSTIFY_CENTER));
		CHECK((layout.items[22].x == 336) && (strcmp(layout.items[22].text, "P2 READY") == 0));
		CHECK((layout.items[23].kind == MAIN_ARCADE_LINK_ITEM_HIGHLIGHT) && (layout.items[23].x == 260) &&
			(layout.items[23].y == 165) && (layout.items[23].w == 244) && (layout.items[23].h == 12));
		CHECK(layout.items[24].kind == MAIN_ARCADE_LINK_ITEM_PANEL);
		/* Small digits in column 0: 1,3 from the left edge, 2,4 to the right. */
		three.select.humanCount = 4u;
		three.select.humans[3] = three.select.humans[1];
		for (uint32_t h = 0; h < 4u; h++) three.select.humans[h].trackID = LV_CRASH_COVE;
		CHECK(BuildInto(&three, &layout) == 1);
		CHECK((layout.items[18].x == 14) && (layout.items[19].x == 232));
		CHECK((layout.items[20].x == 27) && (layout.items[21].x == 245));
		CHECK(layout.items[21].y == 76);
	}
	return 0;
}

/* The countdown shows whole seconds, rounded up. */
static int TestSelectCountdown(void)
{
	static const uint32_t ticks[] = {600u, 599u, 571u, 570u, 31u, 30u, 29u, 1u, 0u, 0xffffffffu};
	static const char *const lines[] = {
		"TIME 20", "TIME 20", "TIME 20", "TIME 19", "TIME 2", "TIME 1", "TIME 1", "TIME 1", "TIME 0",
		"TIME 143165577"};

	for (uint32_t i = 0; i < COUNT(ticks); i++)
	{
		for (uint32_t it = IT_CHARACTER; it <= IT_LAPS; it++)
		{
			struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
			struct MainArcadeLinkLayout layout;
			SetItem(&input, 0u, it);
			input.select.ticksLeft = ticks[i];
			CHECK(BuildInto(&input, &layout) == 1);
			CHECK(layout.items[1].kind == MAIN_ARCADE_LINK_ITEM_TEXT);
			CHECK(layout.items[1].x == 256);
			CHECK(layout.items[1].y == 60);
			CHECK(layout.items[1].font == SMALL);
			CHECK(layout.items[1].flags == (WHITE | MAIN_ARCADE_LINK_JUSTIFY_CENTER));
			CHECK(strcmp(layout.items[1].text, lines[i]) == 0);
		}
	}
	return 0;
}

/* The waiting screen: the title with the lobby dot animation, the local
 * picks, each opponent's progress with its live cursor. */
static int TestSelectWait(void)
{
	static const uint32_t ticks[] = {0u, 14u, 15u, 30u, 45u, 60u};
	static const char *const titles[] = {"WAITING FOR P2", "WAITING FOR P2", "WAITING FOR P2.", "WAITING FOR P2..",
		"WAITING FOR P2...", "WAITING FOR P2"};
	static const uint8_t items[] = {IT_CHARACTER, IT_TRACK, IT_LAPS, IT_DONE};
	static const char *const peerLines[] = {"P2: CHOOSING CHARACTER  DINGODILE", "P2: VOTING TRACK  PAPU'S PYRAMID",
		"P2: VOTING LAPS  5 LAPS", "P2: READY"};

	for (uint32_t t = 0; t < COUNT(ticks); t++)
	{
		for (uint32_t k = 0; k < COUNT(items); k++)
		{
			struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
			const struct ExpectedItem expected[] = {
				TXT(BIG, ORANGE, 40, titles[t]),
				TXT(SMALL, WHITE, 68, "YOUR CHARACTER: CRASH"),
				TXT(SMALL, WHITE, 80, "YOUR TRACK VOTE: ROO'S TUBES"),
				TXT(SMALL, WHITE, 92, "YOUR LAP VOTE: 7 LAPS"),
				TXT(SMALL, PRED, 116, peerLines[k]),
				SELECT_PANEL,
			};
			input.ticksInScreen = ticks[t];
			SetItem(&input, 0u, IT_DONE);
			SetItem(&input, 1u, items[k]);
			input.select.ticksLeft = 0u;
			input.select.status = 1u;
			input.select.humans[0].trackID = LV_ROO_TUBES;
			input.select.humans[0].lapCount = 7u;
			input.select.humans[1].characterID = 5u;
			input.select.humans[1].trackID = LV_PAPU_PYRAMID;
			input.select.humans[1].lapCount = 5u;
			EXPECT_LAYOUT(input, expected);
		}
	}
	{
		/* Cabinet 2 waits for P1, who is not heard yet. */
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "WAITING FOR P1"),
			TXT(SMALL, WHITE, 68, "YOUR CHARACTER: CORTEX"),
			TXT(SMALL, WHITE, 80, "YOUR TRACK VOTE: N. GIN LABS"),
			TXT(SMALL, WHITE, 92, "YOUR LAP VOTE: 3 LAPS"),
			TXT(SMALL, BLUE, 116, "P1: CONNECTING"),
			SELECT_PANEL,
		};
		input.localCab = 2u;
		input.select.localHuman = 1u;
		SetItem(&input, 1u, IT_DONE);
		memset(&input.select.humans[0], 0, sizeof(input.select.humans[0]));
		input.select.humans[1].trackID = LV_N_GIN_LABS;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Four humans: WAITING FOR PLAYERS and one line per opponent. */
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "WAITING FOR PLAYERS..."),
			TXT(SMALL, WHITE, 68, "YOUR CHARACTER: POLAR"),
			TXT(SMALL, WHITE, 80, "YOUR TRACK VOTE: SLIDE COLISEUM"),
			TXT(SMALL, WHITE, 92, "YOUR LAP VOTE: 3 LAPS"),
			TXT(SMALL, BLUE, 116, "P1: VOTING TRACK  DINGO CANYON"),
			TXT(SMALL, PRED, 130, "P2: READY"),
			TXT(SMALL, YELLOW, 144, "P4: CONNECTING"),
			SELECT_PANEL,
		};
		input.ticksInScreen = 45u;
		input.select.humanCount = 4u;
		input.select.localHuman = 2u;
		input.select.humans[2] = input.select.humans[0];
		input.select.humans[2].characterID = 6u;
		input.select.humans[2].trackID = LV_SLIDE_COLISEUM;
		SetItem(&input, 2u, IT_DONE);
		SetItem(&input, 0u, IT_TRACK);
		input.select.humans[0].trackID = LV_DINGO_CANYON;
		SetItem(&input, 1u, IT_DONE);
		EXPECT_LAYOUT(input, expected);
	}
	return 0;
}

/* The result screen: RANDOM marks, reassignment, the bot lines. */
static int TestSelectResult(void)
{
	{
		struct MainArcadeLinkLayoutInput input = MakeResult();
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK TIGER TEMPLE - RANDOM"),
			TXT(SMALL, WHITE, 80, "LAPS 3"),
			TXT(SMALL, BLUE, 100, "P1 CRASH"),
			TXT(SMALL, PRED, 112, "P2 CORTEX"),
			TXT(SMALL, WHITE, 130, "CPU POLAR, N. GIN, TINY, COCO"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Laps drawn, track not; P2 reassigned; six bots on two lines. */
		struct MainArcadeLinkLayoutInput input = MakeResult();
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK SEWER SPEEDWAY"),
			TXT(SMALL, WHITE, 80, "LAPS 7 - RANDOM"),
			TXT(SMALL, BLUE, 100, "P1 DINGODILE"),
			TXT(SMALL, PRED, 112, "P2 CRASH - REASSIGNED"),
			TXT(SMALL, WHITE, 130, "CPU CORTEX, TINY, COCO, N. GIN"),
			TXT(SMALL, WHITE, 142, "POLAR, PURA"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		static const uint8_t bots[6] = {1, 2, 3, 4, 6, 7};
		input.select.trackID = LV_SEWER_SPEEDWAY;
		input.select.trackDrawn = 0u;
		input.select.lapCount = 7u;
		input.select.lapsDrawn = 1u;
		input.select.humanCharacter[0] = 5u;
		input.select.humanCharacter[1] = 0u;
		input.select.characterReassignedMask = 0x2u;
		input.select.botCount = 6u;
		memcpy(input.select.botCharacter, bots, sizeof(bots));
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Four humans, P3 and P4 reassigned, the four longest bot names on
		 * one line (36 characters). */
		struct MainArcadeLinkLayoutInput input = MakeResult();
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK PAPU'S PYRAMID - RANDOM"),
			TXT(SMALL, WHITE, 80, "LAPS 5 - RANDOM"),
			TXT(SMALL, BLUE, 100, "P1 TINY"),
			TXT(SMALL, PRED, 112, "P2 COCO"),
			TXT(SMALL, GREEN, 124, "P3 POLAR - REASSIGNED"),
			TXT(SMALL, YELLOW, 136, "P4 PURA - REASSIGNED"),
			TXT(SMALL, WHITE, 154, "CPU DINGODILE, CORTEX, N. GIN, CRASH"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		static const uint8_t humans[4] = {2, 3, 6, 7};
		static const uint8_t bots[4] = {5, 1, 4, 0};
		input.select.humanCount = 4u;
		for (uint32_t h = 2; h < 4u; h++) input.select.humans[h] = input.select.humans[0];
		input.select.trackID = LV_PAPU_PYRAMID;
		input.select.lapCount = 5u;
		input.select.lapsDrawn = 1u;
		memcpy(input.select.humanCharacter, humans, sizeof(humans));
		input.select.characterReassignedMask = 0xcu;
		memcpy(input.select.botCharacter, bots, sizeof(bots));
		CHECK(strlen("CPU DINGODILE, CORTEX, N. GIN, CRASH") == 36u);
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* One human and seven bots wrap by length. */
		struct MainArcadeLinkLayoutInput input = MakeResult();
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK TIGER TEMPLE - RANDOM"),
			TXT(SMALL, WHITE, 80, "LAPS 3"),
			TXT(SMALL, BLUE, 100, "P1 CRASH"),
			TXT(SMALL, WHITE, 118, "CPU CORTEX, TINY, COCO, N. GIN"),
			TXT(SMALL, WHITE, 130, "DINGODILE, POLAR, PURA"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		static const uint8_t bots[7] = {1, 2, 3, 4, 5, 6, 7};
		input.select.humanCount = 1u;
		memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		input.select.botCount = 7u;
		memcpy(input.select.botCharacter, bots, sizeof(bots));
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* No bots: no CPU line. */
		struct MainArcadeLinkLayoutInput input = MakeResult();
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK TIGER TEMPLE - RANDOM"),
			TXT(SMALL, WHITE, 80, "LAPS 3"),
			TXT(SMALL, BLUE, 100, "P1 CRASH"),
			TXT(SMALL, PRED, 112, "P2 CORTEX"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		input.select.botCount = 0u;
		EXPECT_LAYOUT(input, expected);
	}
	return 0;
}

/* Every select validation failure leaves *out untouched. */
static int TestSelectInvalid(void)
{
#define EXPECT_SELECT_INVALID(base, statement) \
	do { \
		struct MainArcadeLinkLayoutInput bad_ = (base); \
		statement; \
		EXPECT_INVALID(bad_); \
	} while (0)

	const struct MainArcadeLinkLayoutInput item = MakeSelect(SC_SELECT);
	const struct MainArcadeLinkLayoutInput result = MakeResult();
	struct MainArcadeLinkLayoutInput resolvedItem = MakeResult();
	resolvedItem.screen = SC_SELECT;

	/* The bases are valid. */
	{
		struct MainArcadeLinkLayout layout;
		CHECK(BuildInto(&item, &layout) == 1);
		CHECK(BuildInto(&result, &layout) == 1);
		CHECK(BuildInto(&resolvedItem, &layout) == 1);
	}
	EXPECT_SELECT_INVALID(item, bad_.screen = SC_SELECT_RESULT + 1u);
	for (uint32_t s = 0; s < 2u; s++)
	{
		const struct MainArcadeLinkLayoutInput base = (s == 0u) ? item : result;
		EXPECT_SELECT_INVALID(base, bad_.select.active = 0u);
		EXPECT_SELECT_INVALID(base, bad_.select.active = 2u);
		EXPECT_SELECT_INVALID(base, bad_.select.humanCount = 0u);
		EXPECT_SELECT_INVALID(base, bad_.select.humanCount = 5u);
		EXPECT_SELECT_INVALID(base, bad_.select.localHuman = 2u);
		EXPECT_SELECT_INVALID(base, (bad_.select.humanCount = 1u, bad_.select.localHuman = 1u));
		EXPECT_SELECT_INVALID(base, bad_.select.currentItem = 4u);
		EXPECT_SELECT_INVALID(base, bad_.select.status = 5u);
		EXPECT_SELECT_INVALID(base, bad_.select.resolved = 2u);
		EXPECT_SELECT_INVALID(base, bad_.select.peerLockedCharacterMask = 0x100u);
		EXPECT_SELECT_INVALID(base, bad_.select.peerLockedCharacterMask = 0x8000u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[0].present = 0u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].present = 2u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].characterID = 8u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[0].characterID = 15u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].trackID = LV_OXIDE_STATION);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].trackID = LV_TURBO_TRACK);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[0].trackID = 0xffu);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].lapCount = 0u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].lapCount = 4u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[0].lapCount = 9u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].lockMask = 8u);
		EXPECT_SELECT_INVALID(base, bad_.select.humans[1].currentItem = 4u);
		/* The non-select fields are still checked. */
		EXPECT_SELECT_INVALID(base, bad_.localCab = 0u);
		EXPECT_SELECT_INVALID(base, bad_.attract = 1u);
		EXPECT_SELECT_INVALID(base, bad_.rowsEnabled = 2u);
		EXPECT_SELECT_INVALID(base, bad_.lobbyStatus = LS_LOST + 1u);
		EXPECT_SELECT_INVALID(base, bad_.endReason = END_OPPONENT_LEFT + 1u);
	}
	/* SELECT_RESULT needs an outcome. */
	EXPECT_SELECT_INVALID(result, bad_.select.resolved = 0u);
	/* A resolved outcome must be valid on either screen. */
	for (uint32_t s = 0; s < 2u; s++)
	{
		const struct MainArcadeLinkLayoutInput base = (s == 0u) ? resolvedItem : result;
		EXPECT_SELECT_INVALID(base, bad_.select.trackID = LV_OXIDE_STATION);
		EXPECT_SELECT_INVALID(base, bad_.select.trackID = 200u);
		EXPECT_SELECT_INVALID(base, bad_.select.lapCount = 6u);
		EXPECT_SELECT_INVALID(base, bad_.select.trackDrawn = 2u);
		EXPECT_SELECT_INVALID(base, bad_.select.lapsDrawn = 2u);
		EXPECT_SELECT_INVALID(base, bad_.select.characterReassignedMask = 0x4u);
		EXPECT_SELECT_INVALID(base, bad_.select.characterReassignedMask = 0x80u);
		EXPECT_SELECT_INVALID(base, bad_.select.humanCharacter[1] = 8u);
		EXPECT_SELECT_INVALID(base, bad_.select.botCount = 7u);
		EXPECT_SELECT_INVALID(base, bad_.select.botCharacter[3] = 9u);
		EXPECT_SELECT_INVALID(base, (bad_.select.botCount = 6u, bad_.select.botCharacter[5] = 0xffu));
	}
	/* Outcome fields are ignored while unresolved on SELECT, and unused
	 * outcome entries are ignored while resolved. */
	{
		struct MainArcadeLinkLayoutInput unresolved = MakeSelect(SC_SELECT);
		struct MainArcadeLinkLayoutInput tail = MakeResult();
		struct MainArcadeLinkLayout a;
		struct MainArcadeLinkLayout b;
		CHECK(BuildInto(&unresolved, &a) == 1);
		unresolved.select.trackID = LV_OXIDE_STATION;
		unresolved.select.lapCount = 9u;
		unresolved.select.trackDrawn = 7u;
		unresolved.select.botCount = 200u;
		unresolved.select.humanCharacter[0] = 99u;
		unresolved.select.characterReassignedMask = 0xffu;
		unresolved.select.status = 4u;
		CHECK(BuildInto(&unresolved, &b) == 1);
		CHECK(memcmp(&a, &b, sizeof(a)) == 0);
		CHECK(BuildInto(&tail, &a) == 1);
		tail.select.humanCharacter[2] = 0xffu;
		tail.select.humanCharacter[3] = 0xffu;
		tail.select.botCharacter[4] = 0xffu;
		tail.select.botCharacter[7] = 0xffu;
		CHECK(BuildInto(&tail, &b) == 1);
		CHECK(memcmp(&a, &b, sizeof(a)) == 0);
	}
#undef EXPECT_SELECT_INVALID
	return 0;
}

/* The select fields are ignored on every other screen: a garbage select
 * view changes nothing about the 12 accepted screens. */
static int TestSelectFieldsIgnoredElsewhere(void)
{
	for (uint32_t screen = SC_OFF; screen <= SC_EXIT; screen++)
	{
		for (uint8_t attract = 0u; attract <= 1u; attract++)
		{
			struct MainArcadeLinkLayoutInput clean =
				MakeInput(screen, LS_CONNECTING, END_FINISHED, ROW_EXIT, 30u, 2u, 1u, attract);
			struct MainArcadeLinkLayoutInput dirty = clean;
			struct MainArcadeLinkLayout a;
			struct MainArcadeLinkLayout b;
			int valid;

			memset(&dirty.select, 0xee, sizeof(dirty.select));
			valid = BuildInto(&clean, &a);
			CHECK(BuildInto(&dirty, &b) == valid);
			CHECK(memcmp(&a, &b, sizeof(a)) == 0);
		}
	}
	return 0;
}

/* The text box of one item in the width model of the retail font
 * (game/DecalFont.c, game/zGlobal_DATA.c): FONT_BIG and FONT_SMALL advance
 * 17 and 13 px per glyph, ':' and '.' 11 and 7, and are 17 and 8 px tall;
 * centred text starts width / 2 left of x. */
static void TextBox(const struct MainArcadeLinkItem *item, int *x0, int *x1, int *y0, int *y1)
{
	const int big = (item->font == BIG);
	int width = 0;
	for (size_t c = 0; item->text[c] != '\0'; c++)
	{
		if ((item->text[c] == ':') || (item->text[c] == '.')) width += big ? 11 : 7;
		else width += big ? 17 : 13;
	}
	*x0 = item->x - (width / 2);
	*x1 = *x0 + width;
	*y0 = item->y;
	*y1 = item->y + (big ? 17 : 8);
}

/* Checks one layout on a panel at x with width w (the select panel, 4 and
 * 504, or the shared panel, 56 and 400): the item order (TEXT...,
 * HIGHLIGHT, one PANEL last); every text uses only glyphs the retail font
 * maps, fits its buffer, and sits inside the panel with a 4 px margin; no
 * two texts overlap; the highlight sits inside the panel; builds are
 * deterministic regardless of *out's previous contents, with unused items
 * zero. Since SOLO-S3 it covers the shared-panel LOBBY and RESULTS layouts
 * too (CheckPanelLayout), the solo strings among them (SOLO-13). */
static int CheckLayoutOnPanel(const struct MainArcadeLinkLayoutInput *input, int line, int panelX, int panelW)
{
	struct MainArcadeLinkLayout layout;
	struct MainArcadeLinkLayout again;
	const struct MainArcadeLinkItem *panel;
	uint32_t highlights = 0;
	int phase = 0;

	memset(&layout, 0x00, sizeof(layout));
	memset(&again, 0xff, sizeof(again));
	if ((MainArcadeLinkLayout_Build(input, &layout) != 1) || (MainArcadeLinkLayout_Build(input, &again) != 1) ||
		(memcmp(&layout, &again, sizeof(layout)) != 0) || (layout.count < 2u) ||
		(layout.count > MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS))
	{
		fprintf(stderr, "line %d: build failed or is not deterministic\n", line);
		return 1;
	}
	panel = &layout.items[layout.count - 1u];
	if ((panel->kind != MAIN_ARCADE_LINK_ITEM_PANEL) || (panel->x != panelX) || (panel->w != panelW) ||
		(panel->y != 28) || (panel->h != 176) || !AllZero(panel->text, sizeof(panel->text)))
	{
		fprintf(stderr, "line %d: panel\n", line);
		return 1;
	}
	for (uint32_t i = 0; i + 1u < layout.count; i++)
	{
		const struct MainArcadeLinkItem *item = &layout.items[i];
		const int kindPhase = (item->kind == MAIN_ARCADE_LINK_ITEM_TEXT) ? 0 : 1;
		if ((item->kind != MAIN_ARCADE_LINK_ITEM_TEXT) && (item->kind != MAIN_ARCADE_LINK_ITEM_HIGHLIGHT))
		{
			fprintf(stderr, "line %d: item %u kind %u before the panel\n", line, (unsigned)i, (unsigned)item->kind);
			return 1;
		}
		if (kindPhase < phase)
		{
			fprintf(stderr, "line %d: item %u TEXT after a HIGHLIGHT\n", line, (unsigned)i);
			return 1;
		}
		phase = kindPhase;
		if (item->kind == MAIN_ARCADE_LINK_ITEM_HIGHLIGHT)
		{
			highlights++;
			if ((item->x < panel->x) || (item->x + item->w > panel->x + panel->w) || (item->y < panel->y) ||
				(item->y + item->h > panel->y + panel->h) || (item->font != 0u) || (item->flags != 0u) ||
				!AllZero(item->text, sizeof(item->text)))
			{
				fprintf(stderr, "line %d: highlight outside the panel\n", line);
				return 1;
			}
		}
		else
		{
			const char *end = (const char *)memchr(item->text, '\0', sizeof(item->text));
			size_t length;
			int x0, x1, y0, y1;
			if (end == NULL)
			{
				fprintf(stderr, "line %d: item %u text unterminated\n", line, (unsigned)i);
				return 1;
			}
			length = (size_t)(end - item->text);
			if ((length == 0u) || !AllZero(item->text + length, sizeof(item->text) - length) ||
				((item->flags & MAIN_ARCADE_LINK_JUSTIFY_CENTER) == 0u) || (item->w != 0) || (item->h != 0))
			{
				fprintf(stderr, "line %d: item %u text shape\n", line, (unsigned)i);
				return 1;
			}
			for (size_t c = 0; c < length; c++)
			{
				const char ch = item->text[c];
				const int known = ((ch >= 'A') && (ch <= 'Z')) || ((ch >= '0') && (ch <= '9')) || (ch == ' ') ||
				                  (ch == '.') || (ch == ':') || (ch == ',') || (ch == '-') || (ch == '\'');
				if (!known)
				{
					fprintf(stderr, "line %d: item %u glyph '%c'\n", line, (unsigned)i, ch);
					return 1;
				}
			}
			TextBox(item, &x0, &x1, &y0, &y1);
			if ((x0 < panel->x + 4) || (x1 > panel->x + panel->w - 4) || (y0 < panel->y + 4) ||
				(y1 > panel->y + panel->h - 4))
			{
				fprintf(stderr, "line %d: item %u '%s' outside the panel (%d..%d, %d..%d)\n", line, (unsigned)i,
					item->text, x0, x1, y0, y1);
				return 1;
			}
			for (uint32_t j = 0; j < i; j++)
			{
				int a0, a1, b0, b1;
				TextBox(&layout.items[j], &a0, &a1, &b0, &b1);
				if ((x0 < a1) && (a0 < x1) && (y0 < b1) && (b0 < y1))
				{
					fprintf(stderr, "line %d: '%s' overlaps '%s'\n", line, item->text, layout.items[j].text);
					return 1;
				}
			}
		}
	}
	if (highlights > 1u)
	{
		fprintf(stderr, "line %d: more than one highlight\n", line);
		return 1;
	}
	for (uint32_t i = layout.count; i < MAIN_ARCADE_LINK_LAYOUT_MAX_ITEMS; i++)
	{
		if (!AllZero(&layout.items[i], sizeof(layout.items[i])))
		{
			fprintf(stderr, "line %d: unused item %u not zero\n", line, (unsigned)i);
			return 1;
		}
	}
	return 0;
}

static int CheckSelectLayout(const struct MainArcadeLinkLayoutInput *input, int line)
{
	return CheckLayoutOnPanel(input, line, 4, 504);
}

static int CheckPanelLayout(const struct MainArcadeLinkLayoutInput *input, int line)
{
	return CheckLayoutOnPanel(input, line, 56, 400);
}

/* Sweeps the select screens over human counts, local humans, items, peer
 * items, and cursor positions (everyone on one cell, and everyone spread
 * out): no text leaves the panel or overlaps another. Then every result
 * shape with the longest names, and the longest waiting-screen lines. */
static int TestSelectGeometrySweep(void)
{
	static const uint32_t itemCounts[3] = {8u, 16u, 3u};
	uint32_t built = 0;

	for (uint32_t count = 1u; count <= 4u; count++)
	for (uint32_t local = 0u; local < count; local++)
	for (uint32_t item = IT_CHARACTER; item <= IT_DONE; item++)
	for (uint32_t spread = 0u; spread < 2u; spread++)
	for (uint32_t cell = 0u; cell < 16u; cell++)
	for (uint32_t peerItem = IT_CHARACTER; peerItem <= IT_DONE; peerItem++)
	{
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		const uint32_t n = (item == IT_DONE) ? 16u : itemCounts[item];

		if (cell >= n) continue;
		input.ticksInScreen = cell * 15u;
		input.select.humanCount = (uint8_t)count;
		input.select.localHuman = (uint8_t)local;
		input.select.ticksLeft = cell * 97u;
		for (uint32_t h = 0; h < 4u; h++)
		{
			const uint32_t at = (spread != 0u) ? ((cell + (h * 5u)) % n) : cell;
			memset(&input.select.humans[h], 0, sizeof(input.select.humans[h]));
			if (h >= count) continue;
			input.select.humans[h].present = (uint8_t)(((h == local) || (h != 3u) || ((cell % 2u) == 0u)) ? 1u : 0u);
			input.select.humans[h].characterID = k_characterOrder[at % 8u];
			input.select.humans[h].trackID = k_trackOrder[at % 16u];
			input.select.humans[h].lapCount = k_lapOrder[at % 3u];
			SetItem(&input, h, (h == local) ? item : peerItem);
		}
		input.select.peerLockedCharacterMask = (uint16_t)((spread != 0u) ? (0xa5u >> (cell % 4u)) : 0u);
		if (CheckSelectLayout(&input, __LINE__) != 0) return 1;
		built++;
	}
	CHECK(built > 1000u);

	for (uint32_t count = 1u; count <= 4u; count++)
	for (uint32_t bots = 0u; bots <= 8u - count; bots++)
	for (uint32_t flags = 0u; flags < 4u; flags++)
	for (uint32_t names = 0u; names < 8u; names++)
	{
		struct MainArcadeLinkLayoutInput input = MakeResult();
		input.select.humanCount = (uint8_t)count;
		for (uint32_t h = 0; h < 4u; h++)
		{
			input.select.humans[h] = input.select.humans[0];
			input.select.humanCharacter[h] = (uint8_t)((names + h) % 8u);
			if (h >= count) memset(&input.select.humans[h], 0, sizeof(input.select.humans[h]));
		}
		input.select.trackID = LV_SLIDE_COLISEUM;
		input.select.lapCount = 7u;
		input.select.trackDrawn = (uint8_t)(flags & 1u);
		input.select.lapsDrawn = (uint8_t)((flags >> 1) & 1u);
		input.select.characterReassignedMask = (uint8_t)((1u << count) - 1u);
		input.select.botCount = (uint8_t)bots;
		/* Duplicates on purpose: the longest name everywhere. */
		for (uint32_t b = 0; b < 8u; b++) input.select.botCharacter[b] = (uint8_t)(((b + names) % 2u == 0u) ? 5u : names);
		if (CheckSelectLayout(&input, __LINE__) != 0) return 1;
	}

	{
		struct MainArcadeLinkLayoutInput input = MakeSelect(SC_SELECT);
		input.select.humanCount = 4u;
		input.ticksInScreen = 45u;
		for (uint32_t h = 0; h < 4u; h++)
		{
			input.select.humans[h] = input.select.humans[0];
			input.select.humans[h].characterID = 5u;
			input.select.humans[h].trackID = LV_SEWER_SPEEDWAY;
		}
		SetItem(&input, 0u, IT_DONE);
		SetItem(&input, 1u, IT_CHARACTER);
		SetItem(&input, 2u, IT_TRACK);
		SetItem(&input, 3u, IT_LAPS);
		if (CheckSelectLayout(&input, __LINE__) != 0) return 1;
	}
	return 0;
}

/* ---- Solo screens (docs/SOLO_CAB_MILESTONE.md SOLO-S3) ---- */

#define ROW_RACE_AGAIN NATIVE_ARCADE_FLOW_ROW_RACE_AGAIN
#define ROW_LOBBY NATIVE_ARCADE_FLOW_ROW_LOBBY

/* A solo select (SOLO-5): MakeSelect with one human, the local cabinet
 * alone. */
static struct MainArcadeLinkLayoutInput MakeSoloSelect(uint32_t screen)
{
	struct MainArcadeLinkLayoutInput input = MakeSelect(screen);

	input.solo = 1u;
	input.select.humanCount = 1u;
	memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
	return input;
}

/* A solo RESULTS input. */
static struct MainArcadeLinkLayoutInput MakeSoloResults(
	uint32_t endReason, uint32_t selectedRow, uint8_t rowsEnabled, uint8_t peerHeard)
{
	struct MainArcadeLinkLayoutInput input =
		MakeInput(SC_RESULTS, LS_WAITING, endReason, selectedRow, 30u, 1u, rowsEnabled, 0u);

	input.solo = 1u;
	input.peerHeard = peerHeard;
	return input;
}

/* Returns 1 when no text item of the layout contains `word`. */
static int NoTextContains(const struct MainArcadeLinkLayout *layout, const char *word)
{
	for (uint32_t i = 0; i < layout->count; i++)
	{
		if ((layout->items[i].kind == MAIN_ARCADE_LINK_ITEM_TEXT) && (strstr(layout->items[i].text, word) != NULL))
			return 0;
	}
	return 1;
}

/* SOLO-13 and SOLO-12: while the offer stands a WAITING, CONNECTING, or LOST
 * lobby shows the solo lines, dots animating; READY and REJECTED, and every
 * lobby without the offer, are exactly the linked lobby. */
static int TestSoloLobby(void)
{
	const struct ExpectedItem offered[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OTHER CABINET"),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		TXT(SMALL, ORANGE, 145, "PRESS START TO RACE SOLO"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem offeredCab2[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OTHER CABINET..."),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 2"),
		TXT(SMALL, ORANGE, 145, "PRESS START TO RACE SOLO"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};
	const struct ExpectedItem offeredDot[] = {
		TXT(BIG, ORANGE, 40, "ARCADE LINK"),
		TXT(SMALL, WHITE, 90, "WAITING FOR OTHER CABINET."),
		TXT(SMALL, WHITE, 110, "THIS CABINET: CAB 1"),
		TXT(SMALL, ORANGE, 145, "PRESS START TO RACE SOLO"),
		TXT(SMALL, ORANGE, 186, "TRIANGLE: BACK"),
		PANEL,
	};

	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	{
		struct MainArcadeLinkLayoutInput input = MakeInput(SC_LOBBY, status, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u);
		struct MainArcadeLinkLayout linked;
		struct MainArcadeLinkLayout solo;

		CHECK(BuildInto(&input, &linked) == 1);
		input.soloOffered = 1u;
		if ((status == LS_READY) || (status == LS_REJECTED))
		{
			/* No offer is shown once the peer is heard. */
			CHECK(BuildInto(&input, &solo) == 1);
			CHECK(memcmp(&linked, &solo, sizeof(linked)) == 0);
			continue;
		}
		EXPECT_LAYOUT(input, offered);
		input.localCab = 2u;
		input.ticksInScreen = 45u;
		EXPECT_LAYOUT(input, offeredCab2);
		input.localCab = 1u;
		input.ticksInScreen = 15u;
		EXPECT_LAYOUT(input, offeredDot);
		/* The end reason, focused row, rowsEnabled, and peerHeard do not
		 * change it. */
		input.ticksInScreen = 60u;
		input.endReason = END_DESYNC;
		input.selectedRow = ROW_EXIT;
		input.rowsEnabled = 1u;
		input.peerHeard = 1u;
		EXPECT_LAYOUT(input, offered);
	}
	return 0;
}

/* SOLO-8 and SOLO-7: the solo RESULTS rows, the default cursor on RACE
 * AGAIN, the RACE ERROR title for a LINK_ERROR end, and the notice while
 * the other cabinet was heard. No solo results text names the link. */
static int TestSoloResults(void)
{
	static const uint32_t reasons[] = {
		END_NONE, END_FINISHED, END_PEER_TIMEOUT, END_DESYNC, END_LINK_ERROR, END_OPPONENT_LEFT};
	static const char *const titles[] = {"RESULTS", "RACE COMPLETE", "RESULTS", "RESULTS", "RACE ERROR", "RESULTS"};
	static const uint32_t titleColors[] = {ORANGE, ORANGE, ORANGE, ORANGE, RED, ORANGE};

	for (uint32_t i = 0; i < COUNT(reasons); i++)
	{
		const struct ExpectedItem focusRaceAgain[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(BIG, ORANGE, 120, "RACE AGAIN"),
			TXT(BIG, ORANGE, 145, "LOBBY"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			HIGHLIGHT(117),
			PANEL,
		};
		const struct ExpectedItem focusLobbyHeard[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(SMALL, WHITE, 90, "OTHER CABINET IS READY"),
			TXT(BIG, ORANGE, 120, "RACE AGAIN"),
			TXT(BIG, ORANGE, 145, "LOBBY"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			HIGHLIGHT(142),
			PANEL,
		};
		const struct ExpectedItem disabledHeard[] = {
			TXT(BIG, titleColors[i], 40, titles[i]),
			TXT(SMALL, WHITE, 90, "OTHER CABINET IS READY"),
			TXT(BIG, GRAY, 120, "RACE AGAIN"),
			TXT(BIG, GRAY, 145, "LOBBY"),
			TXT(SMALL, ORANGE, 186, "CROSS: SELECT"),
			PANEL,
		};

		EXPECT_LAYOUT(MakeSoloResults(reasons[i], ROW_RACE_AGAIN, 1u, 0u), focusRaceAgain);
		EXPECT_LAYOUT(MakeSoloResults(reasons[i], ROW_LOBBY, 1u, 1u), focusLobbyHeard);
		EXPECT_LAYOUT(MakeSoloResults(reasons[i], ROW_LOBBY, 0u, 1u), disabledHeard);
		/* Any row other than ROW_LOBBY means RACE AGAIN, as in linked. */
		EXPECT_LAYOUT(MakeSoloResults(reasons[i], 0xffffffffu, 1u, 0u), focusRaceAgain);
		/* The lobby status never changes it (solo reads none). */
		{
			struct MainArcadeLinkLayoutInput input = MakeSoloResults(reasons[i], ROW_RACE_AGAIN, 1u, 0u);
			input.lobbyStatus = LS_REJECTED;
			input.localCab = 2u;
			EXPECT_LAYOUT(input, focusRaceAgain);
		}
	}
	CHECK(ROW_RACE_AGAIN == ROW_REMATCH);
	CHECK(ROW_LOBBY == ROW_EXIT);

	/* No "LINK" wording on any solo results screen (SOLO-7). */
	for (uint32_t i = 0; i < COUNT(reasons); i++)
	for (uint32_t row = 0u; row < 2u; row++)
	for (uint8_t enabled = 0u; enabled <= 1u; enabled++)
	for (uint8_t heard = 0u; heard <= 1u; heard++)
	{
		struct MainArcadeLinkLayoutInput input = MakeSoloResults(reasons[i], row, enabled, heard);
		struct MainArcadeLinkLayout layout;
		CHECK(BuildInto(&input, &layout) == 1);
		CHECK(NoTextContains(&layout, "LINK"));
		CHECK(NoTextContains(&layout, "REMATCH"));
		CHECK(NoTextContains(&layout, "OPPONENT"));
	}
	return 0;
}

/* SOLO-5: the solo select draws the local human alone. No opponent footer,
 * no GRAY taken character (a stray peer mask is ignored), a GET READY
 * waiting title without dots or opponent lines, and SELECT_RESULT as the
 * one-human linked screen. */
static int TestSoloSelect(void)
{
	{
		struct MainArcadeLinkLayoutInput input = MakeSoloSelect(SC_SELECT);
		struct MainArcadeLinkLayoutInput linked;
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "SELECT CHARACTER"),
			TXT(SMALL, WHITE, 60, "TIME 20"),
			TXTAT(BIG, ORANGE, 130, 78, "CRASH"),
			TXTAT(BIG, ORANGE, 130, 102, "CORTEX"),
			TXTAT(BIG, ORANGE, 130, 126, "TINY"),
			TXTAT(BIG, ORANGE, 130, 150, "COCO"),
			TXTAT(BIG, ORANGE, 382, 78, "N. GIN"),
			TXTAT(BIG, ORANGE, 382, 102, "DINGODILE"),
			TXTAT(BIG, ORANGE, 382, 126, "POLAR"),
			TXTAT(BIG, ORANGE, 382, 150, "PURA"),
			TXTAT(BIG, BLUE, 25, 78, "P1"),
			HL(8, 75, 244, 21),
			SELECT_PANEL,
		};
		EXPECT_LAYOUT(input, expected);
		/* The same as the one-human linked screen. */
		linked = input;
		linked.solo = 0u;
		{
			struct MainArcadeLinkLayout a;
			struct MainArcadeLinkLayout b;
			CHECK(BuildInto(&input, &a) == 1);
			CHECK(BuildInto(&linked, &b) == 1);
			CHECK(memcmp(&a, &b, sizeof(a)) == 0);
		}
		/* A peer mask never greys a character in solo. */
		input.select.peerLockedCharacterMask = 0xffu;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Track and laps: the local marker and highlight, no footer. */
		struct MainArcadeLinkLayoutInput input = MakeSoloSelect(SC_SELECT);
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "VOTE LAPS"),
			TXT(SMALL, WHITE, 60, "TIME 1"),
			TXTAT(BIG, ORANGE, 256, 86, "3 LAPS"),
			TXTAT(BIG, ORANGE, 256, 111, "5 LAPS"),
			TXTAT(BIG, ORANGE, 256, 136, "7 LAPS"),
			TXTAT(BIG, BLUE, 151, 111, "P1"),
			HL(134, 108, 244, 21),
			SELECT_PANEL,
		};
		SetItem(&input, 0u, IT_LAPS);
		input.select.humans[0].lapCount = 5u;
		input.select.ticksLeft = 1u;
		EXPECT_LAYOUT(input, expected);
	}
	{
		/* Done: GET READY, never "WAITING FOR", at any tick. */
		static const uint32_t ticks[] = {0u, 15u, 30u, 45u};
		for (uint32_t t = 0; t < COUNT(ticks); t++)
		{
			struct MainArcadeLinkLayoutInput input = MakeSoloSelect(SC_SELECT);
			const struct ExpectedItem expected[] = {
				TXT(BIG, ORANGE, 40, "GET READY"),
				TXT(SMALL, WHITE, 68, "YOUR CHARACTER: CRASH"),
				TXT(SMALL, WHITE, 80, "YOUR TRACK VOTE: CRASH COVE"),
				TXT(SMALL, WHITE, 92, "YOUR LAP VOTE: 3 LAPS"),
				SELECT_PANEL,
			};
			input.ticksInScreen = ticks[t];
			SetItem(&input, 0u, IT_DONE);
			input.select.ticksLeft = 0u;
			input.select.status = 3u;
			EXPECT_LAYOUT(input, expected);
		}
	}
	{
		/* SELECT_RESULT: one human line, the LOAD_Robots1P bots for CRASH on
		 * two lines, GET READY; the same as the one-human linked screen. */
		struct MainArcadeLinkLayoutInput input = MakeResult();
		struct MainArcadeLinkLayoutInput linked;
		static const uint8_t bots[7] = {1, 2, 3, 4, 5, 6, 7};
		const struct ExpectedItem expected[] = {
			TXT(BIG, ORANGE, 40, "MATCH SET"),
			TXT(SMALL, WHITE, 66, "TRACK TIGER TEMPLE"),
			TXT(SMALL, WHITE, 80, "LAPS 3"),
			TXT(SMALL, BLUE, 100, "P1 CRASH"),
			TXT(SMALL, WHITE, 118, "CPU CORTEX, TINY, COCO, N. GIN"),
			TXT(SMALL, WHITE, 130, "DINGODILE, POLAR, PURA"),
			TXT(SMALL, ORANGE, 186, "GET READY"),
			SELECT_PANEL,
		};
		input.solo = 1u;
		input.select.humanCount = 1u;
		memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		input.select.humanCharacter[1] = 0u;
		input.select.trackDrawn = 0u;
		input.select.botCount = 7u;
		memcpy(input.select.botCharacter, bots, sizeof(bots));
		EXPECT_LAYOUT(input, expected);
		linked = input;
		linked.solo = 0u;
		EXPECT_LAYOUT(linked, expected);
	}
	return 0;
}

/* The solo fields' validation: range, the screens each may appear on, and
 * one human on a solo select. */
static int TestSoloInvalid(void)
{
	struct MainArcadeLinkLayoutInput input;

	for (uint32_t screen = SC_OFF; screen <= SC_SELECT_RESULT; screen++)
	{
		const int selectScreen = (screen == SC_SELECT) || (screen == SC_SELECT_RESULT);
		const int soloScreen = selectScreen || (screen == SC_RACING) || (screen == SC_RESULTS);

		input = selectScreen ? ((screen == SC_SELECT) ? MakeSoloSelect(SC_SELECT) : MakeResult())
		                     : MakeInput(screen, LS_WAITING, END_FINISHED, ROW_REMATCH, 0u, 1u, 0u, 0u);
		if (screen == SC_SELECT_RESULT)
		{
			input.select.humanCount = 1u;
			memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		}
		input.solo = 1u;
		if (soloScreen)
		{
			struct MainArcadeLinkLayout layout;
			CHECK(BuildInto(&input, &layout) == 1);
			if (screen == SC_RACING) CHECK(layout.count == 0u);
		}
		else
		{
			EXPECT_INVALID(input);
		}
		input.solo = 0u;
		input.soloOffered = 1u;
		if (screen == SC_LOBBY)
		{
			struct MainArcadeLinkLayout layout;
			CHECK(BuildInto(&input, &layout) == 1);
		}
		else
		{
			EXPECT_INVALID(input);
		}
		input.soloOffered = 0u;
		input.solo = 2u;
		EXPECT_INVALID(input);
		input.solo = 0xffu;
		EXPECT_INVALID(input);
		input.solo = 0u;
		input.soloOffered = 2u;
		EXPECT_INVALID(input);
		input.soloOffered = 0u;
		input.peerHeard = 2u;
		EXPECT_INVALID(input);
	}
	/* Solo and the offer together (the offer is LOBBY only, solo never). */
	input = MakeInput(SC_LOBBY, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 0u);
	input.solo = 1u;
	input.soloOffered = 1u;
	EXPECT_INVALID(input);
	/* The attract layout with a solo field. */
	input = MakeInput(SC_OFF, LS_WAITING, END_NONE, ROW_REMATCH, 0u, 1u, 0u, 1u);
	input.soloOffered = 1u;
	EXPECT_INVALID(input);
	/* A solo select has one human. */
	input = MakeSelect(SC_SELECT);
	input.solo = 1u;
	EXPECT_INVALID(input);
	input = MakeResult();
	input.solo = 1u;
	EXPECT_INVALID(input);
	return 0;
}

/* SOLO-12: with solo and the offer off, the solo-only inputs change nothing
 * on any screen: peerHeard (drawn on solo RESULTS alone) and soloReserved
 * leave every linked layout byte-identical, and so every expectation above
 * that builds with them zero stands for the linked screens. */
static int TestSoloFieldsIgnoredWhenLinked(void)
{
	static const uint32_t ticks[] = {0u, 15u, 45u};
	uint32_t compared = 0;

	for (uint32_t screen = SC_OFF; screen <= SC_EXIT; screen++)
	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	for (uint32_t reason = END_NONE; reason <= END_OPPONENT_LEFT; reason++)
	for (uint32_t row = 0u; row < 2u; row++)
	for (uint32_t t = 0u; t < COUNT(ticks); t++)
	for (uint8_t cab = 1u; cab <= 2u; cab++)
	for (uint8_t enabled = 0u; enabled <= 1u; enabled++)
	for (uint8_t attract = 0u; attract <= 1u; attract++)
	{
		struct MainArcadeLinkLayoutInput clean = MakeInput(screen, status, reason, row, ticks[t], cab, enabled, attract);
		struct MainArcadeLinkLayoutInput dirty = clean;
		struct MainArcadeLinkLayout a;
		struct MainArcadeLinkLayout b;
		int valid;

		dirty.peerHeard = 1u;
		dirty.soloReserved = 0xabu;
		valid = BuildInto(&clean, &a);
		CHECK(BuildInto(&dirty, &b) == valid);
		CHECK(memcmp(&a, &b, sizeof(a)) == 0);
		compared++;
	}
	CHECK(compared > 0u);
	{
		struct MainArcadeLinkLayoutInput clean = MakeSelect(SC_SELECT);
		struct MainArcadeLinkLayoutInput dirty = clean;
		struct MainArcadeLinkLayout a;
		struct MainArcadeLinkLayout b;

		dirty.peerHeard = 1u;
		dirty.soloReserved = 0xabu;
		CHECK(BuildInto(&clean, &a) == 1);
		CHECK(BuildInto(&dirty, &b) == 1);
		CHECK(memcmp(&a, &b, sizeof(a)) == 0);
	}
	return 0;
}

/* The glyph and panel checks (CheckLayoutOnPanel) on every shared-panel
 * layout, linked and solo: every LOBBY with and without the offer, every
 * RESULTS end reason, row, and notice state, MATCH_FOUND, REMATCH_WAIT,
 * EXIT, and the attract layout; then the solo select screens over every
 * item and cursor cell, and every solo result shape (SOLO-13). */
static int TestSoloGlyphsAndPanel(void)
{
	static const uint32_t ticks[] = {0u, 15u, 30u, 45u};
	static const uint32_t itemCounts[3] = {8u, 16u, 3u};
	uint32_t checked = 0;

	for (uint32_t status = LS_WAITING; status <= LS_LOST; status++)
	for (uint32_t t = 0u; t < COUNT(ticks); t++)
	for (uint8_t cab = 1u; cab <= 2u; cab++)
	for (uint8_t offered = 0u; offered <= 1u; offered++)
	{
		struct MainArcadeLinkLayoutInput input = MakeInput(SC_LOBBY, status, END_NONE, ROW_REMATCH, ticks[t], cab, 0u, 0u);
		input.soloOffered = offered;
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		checked++;
	}
	for (uint32_t reason = END_NONE; reason <= END_OPPONENT_LEFT; reason++)
	for (uint32_t row = 0u; row < 2u; row++)
	for (uint8_t enabled = 0u; enabled <= 1u; enabled++)
	for (uint8_t solo = 0u; solo <= 1u; solo++)
	for (uint8_t heard = 0u; heard <= 1u; heard++)
	{
		struct MainArcadeLinkLayoutInput input = MakeInput(SC_RESULTS, LS_WAITING, reason, row, 30u, 1u, enabled, 0u);
		input.solo = solo;
		input.peerHeard = heard;
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		checked++;
	}
	for (uint32_t reason = END_NONE; reason <= END_OPPONENT_LEFT; reason++)
	for (uint32_t t = 0u; t < COUNT(ticks); t++)
	for (uint8_t cab = 1u; cab <= 2u; cab++)
	{
		struct MainArcadeLinkLayoutInput input = MakeInput(SC_MATCH_FOUND, LS_READY, reason, ROW_REMATCH, ticks[t], cab, 0u, 0u);
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		input.screen = SC_REMATCH_WAIT;
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		input.screen = SC_EXIT;
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		input.screen = SC_OFF;
		input.attract = 1u;
		if (CheckPanelLayout(&input, __LINE__) != 0) return 1;
		checked += 4u;
	}
	CHECK(checked > 100u);

	for (uint32_t item = IT_CHARACTER; item <= IT_DONE; item++)
	for (uint32_t cell = 0u; cell < 16u; cell++)
	{
		struct MainArcadeLinkLayoutInput input = MakeSoloSelect(SC_SELECT);
		const uint32_t n = (item == IT_DONE) ? 16u : itemCounts[item];

		if (cell >= n) continue;
		input.ticksInScreen = cell * 15u;
		input.select.ticksLeft = cell * 97u;
		input.select.humans[0].characterID = k_characterOrder[cell % 8u];
		input.select.humans[0].trackID = k_trackOrder[cell % 16u];
		input.select.humans[0].lapCount = k_lapOrder[cell % 3u];
		SetItem(&input, 0u, item);
		input.select.peerLockedCharacterMask = (uint16_t)(0xa5u >> (cell % 4u));
		if (CheckSelectLayout(&input, __LINE__) != 0) return 1;
	}
	for (uint32_t human = 0u; human < 8u; human++)
	for (uint32_t bots = 0u; bots <= 7u; bots++)
	{
		struct MainArcadeLinkLayoutInput input = MakeResult();
		uint32_t b = 0u;

		input.solo = 1u;
		input.select.humanCount = 1u;
		memset(&input.select.humans[1], 0, sizeof(input.select.humans[1]));
		input.select.humanCharacter[0] = (uint8_t)human;
		input.select.humanCharacter[1] = 0u;
		input.select.trackID = LV_SLIDE_COLISEUM;
		input.select.lapCount = 7u;
		input.select.botCount = (uint8_t)bots;
		memset(input.select.botCharacter, 0, sizeof(input.select.botCharacter));
		for (uint32_t c = 0u; (c < 8u) && (b < bots); c++)
		{
			if (c != human) input.select.botCharacter[b++] = (uint8_t)c;
		}
		if (CheckSelectLayout(&input, __LINE__) != 0) return 1;
	}
	return 0;
}

/* MainArcadeLinkLayout_InputFromHostView (MS-10b): NULL leaves *input
 * untouched; otherwise every field is copied from the host view, field for
 * field, and every reserved byte is zero whatever the view or the input
 * held before. Distinct byte values make a swapped or dropped field show. */
static int TestInputFromHostView(void)
{
	struct NativeArcadeLinkHostView view;
	struct MainArcadeLinkLayoutInput input;
	struct MainArcadeLinkLayoutInput sentinel;
	uint32_t i;

	memset(&view, 0xEE, sizeof(view));
	memset(&input, 0x5A, sizeof(input));
	sentinel = input;
	CHECK(MainArcadeLinkLayout_InputFromHostView(NULL, &input) == 0);
	CHECK(memcmp(&input, &sentinel, sizeof(input)) == 0);
	CHECK(MainArcadeLinkLayout_InputFromHostView(&view, NULL) == 0);
	CHECK(MainArcadeLinkLayout_InputFromHostView(NULL, NULL) == 0);

	view.screen = 0x01020304u;
	view.lobbyStatus = 0x05060708u;
	view.endReason = 0x090A0B0Cu;
	view.selectedRow = 0x0D0E0F10u;
	view.ticksInScreen = 0x11121314u;
	view.localCab = 0x15u;
	view.rowsEnabled = 0x16u;
	view.attract = 0x17u;
	view.select.active = 0x21u;
	view.select.humanCount = 0x22u;
	view.select.localHuman = 0x23u;
	view.select.currentItem = 0x24u;
	view.select.ticksLeft = 0x25262728u;
	view.select.status = 0x29u;
	view.select.resolved = 0x2Au;
	view.select.trackID = 0x2Bu;
	view.select.lapCount = 0x2Cu;
	view.select.trackDrawn = 0x2Du;
	view.select.lapsDrawn = 0x2Eu;
	view.select.characterReassignedMask = 0x2Fu;
	view.select.botCount = 0x30u;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		view.select.humanCharacter[i] = (uint8_t)(0x31u + i);
	}
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_BOTS; i++)
	{
		view.select.botCharacter[i] = (uint8_t)(0x35u + i);
	}
	view.select.peerLockedCharacterMask = 0x3D3Eu;
	for (i = 0u; i < NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS; i++)
	{
		view.select.humans[i].present = (uint8_t)(0x40u + 0x10u * i);
		view.select.humans[i].characterID = (uint8_t)(0x41u + 0x10u * i);
		view.select.humans[i].trackID = (uint8_t)(0x42u + 0x10u * i);
		view.select.humans[i].lapCount = (uint8_t)(0x43u + 0x10u * i);
		view.select.humans[i].lockMask = (uint8_t)(0x44u + 0x10u * i);
		view.select.humans[i].currentItem = (uint8_t)(0x45u + 0x10u * i);
	}
	/* localMenuEvent is not a layout input (it drives menu sounds, not
	 * drawing): a distinct value that must not reach input.reserved. Every
	 * select reserved byte stays 0xEE. */
	view.localMenuEvent = 0x18u;
	/* The solo group (SOLO-S3): mapped field for field; the view's reserved
	 * byte stays 0xEE and must not reach input.soloReserved. */
	view.solo = 0x19u;
	view.soloOffered = 0x1Au;
	view.peerHeard = 0x1Bu;

	CHECK(MainArcadeLinkLayout_InputFromHostView(&view, &input) == 1);
	CHECK(input.screen == 0x01020304u);
	CHECK(input.lobbyStatus == 0x05060708u);
	CHECK(input.endReason == 0x090A0B0Cu);
	CHECK(input.selectedRow == 0x0D0E0F10u);
	CHECK(input.ticksInScreen == 0x11121314u);
	CHECK(input.localCab == 0x15u);
	CHECK(input.rowsEnabled == 0x16u);
	CHECK(input.attract == 0x17u);
	CHECK(input.reserved == 0u);
	CHECK(input.solo == 0x19u);
	CHECK(input.soloOffered == 0x1Au);
	CHECK(input.peerHeard == 0x1Bu);
	CHECK(input.soloReserved == 0u);
	CHECK(input.select.active == 0x21u);
	CHECK(input.select.humanCount == 0x22u);
	CHECK(input.select.localHuman == 0x23u);
	CHECK(input.select.currentItem == 0x24u);
	CHECK(input.select.ticksLeft == 0x25262728u);
	CHECK(input.select.status == 0x29u);
	CHECK(input.select.resolved == 0x2Au);
	CHECK(input.select.trackID == 0x2Bu);
	CHECK(input.select.lapCount == 0x2Cu);
	CHECK(input.select.trackDrawn == 0x2Du);
	CHECK(input.select.lapsDrawn == 0x2Eu);
	CHECK(input.select.characterReassignedMask == 0x2Fu);
	CHECK(input.select.botCount == 0x30u);
	for (i = 0u; i < MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS; i++)
	{
		CHECK(input.select.humanCharacter[i] == (uint8_t)(0x31u + i));
	}
	for (i = 0u; i < MAIN_ARCADE_LINK_LAYOUT_MAX_BOTS; i++)
	{
		CHECK(input.select.botCharacter[i] == (uint8_t)(0x35u + i));
	}
	CHECK(input.select.peerLockedCharacterMask == 0x3D3Eu);
	CHECK(input.select.reserved[0] == 0u);
	CHECK(input.select.reserved[1] == 0u);
	for (i = 0u; i < MAIN_ARCADE_LINK_LAYOUT_MAX_HUMANS; i++)
	{
		CHECK(input.select.humans[i].present == (uint8_t)(0x40u + 0x10u * i));
		CHECK(input.select.humans[i].characterID == (uint8_t)(0x41u + 0x10u * i));
		CHECK(input.select.humans[i].trackID == (uint8_t)(0x42u + 0x10u * i));
		CHECK(input.select.humans[i].lapCount == (uint8_t)(0x43u + 0x10u * i));
		CHECK(input.select.humans[i].lockMask == (uint8_t)(0x44u + 0x10u * i));
		CHECK(input.select.humans[i].currentItem == (uint8_t)(0x45u + 0x10u * i));
		CHECK(input.select.humans[i].reserved[0] == 0u);
		CHECK(input.select.humans[i].reserved[1] == 0u);
	}

	/* The view is only read. */
	CHECK(view.screen == 0x01020304u);
	CHECK(view.localMenuEvent == 0x18u);
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
	if (TestSelectConstants() != 0) return 1;
	if (TestSelectCharacter() != 0) return 1;
	if (TestSelectCharacterTakenAndCursor() != 0) return 1;
	if (TestSelectMarkersOneAndTwo() != 0) return 1;
	if (TestSelectTrack() != 0) return 1;
	if (TestSelectLaps() != 0) return 1;
	if (TestSelectFourHumans() != 0) return 1;
	if (TestSelectCountdown() != 0) return 1;
	if (TestSelectWait() != 0) return 1;
	if (TestSelectResult() != 0) return 1;
	if (TestSelectInvalid() != 0) return 1;
	if (TestSelectFieldsIgnoredElsewhere() != 0) return 1;
	if (TestSelectGeometrySweep() != 0) return 1;
	if (TestSoloLobby() != 0) return 1;
	if (TestSoloResults() != 0) return 1;
	if (TestSoloSelect() != 0) return 1;
	if (TestSoloInvalid() != 0) return 1;
	if (TestSoloFieldsIgnoredWhenLinked() != 0) return 1;
	if (TestSoloGlyphsAndPanel() != 0) return 1;
	if (TestInputFromHostView() != 0) return 1;
	printf("main_arcade_link_layout_test: ok\n");
	return 0;
}
