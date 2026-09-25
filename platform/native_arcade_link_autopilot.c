#include "platform/native_arcade_link_autopilot.h"

#include "platform/native_arcade_flow.h"
#include "platform/native_arcade_link_host.h"
#include "platform/native_arcade_menu_input.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Internal arcade-link autopilot (docs/RACE_LAUNCH_MILESTONE.md RL-15). Pure
 * decision and bookkeeping over caller-owned state; the report writer is the
 * only I/O. The game glue (game/MAIN/MainArcadeLinkAutopilot.c) owns the one
 * run's state and feeds it the host view.
 */

static const char k_autopilotOption[] = "--arcade-link-autopilot";
static const char k_raceTicksOption[] = "--arcade-link-autopilot-race-ticks";

/* The RESULTS row a decision confirmed, stored + 1 so 0 means none. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_CONFIRMED_NONE 0u

static void NativeArcadeLinkAutopilot_Fail(struct NativeArcadeLinkAutopilot *autopilot, uint32_t result)
{
	autopilot->result = result;
	autopilot->done = 1u;
}

void NativeArcadeLinkAutopilotOptions_SetDefaults(struct NativeArcadeLinkAutopilotOptions *options)
{
	if (options == NULL)
	{
		return;
	}
	memset(options, 0, sizeof(*options));
}

/* The race tick count: 1 to 5 decimal digits, nothing else, with a value of
 * 1..RACE_TICKS_MAX (LR-60). */
static int NativeArcadeLinkAutopilotOptions_ParseRaceTicks(const char *text, uint32_t *value)
{
	uint32_t result = 0u;
	size_t digits = 0u;

	for (; text[digits] != '\0'; digits++)
	{
		if ((text[digits] < '0') || (text[digits] > '9') || (digits == 5u))
		{
			return 0;
		}
		result = (result * 10u) + (uint32_t)(text[digits] - '0');
	}
	if ((digits == 0u) || (result < 1u) || (result > NATIVE_ARCADE_LINK_AUTOPILOT_RACE_TICKS_MAX))
	{
		return 0;
	}
	*value = result;
	return 1;
}

int NativeArcadeLinkAutopilotOptions_ApplyArgs(int argc, char *argv[], struct NativeArcadeLinkAutopilotOptions *options)
{
	struct NativeArcadeLinkAutopilotOptions candidate;
	int seen = 0;
	int seenRaceTicks = 0;

	if ((options == NULL) || (argc < 0) || ((argc > 0) && (argv == NULL)))
	{
		return 0;
	}
	candidate = *options;
	for (int index = 1; index < argc; index++)
	{
		const char *arg = argv[index];
		const char *value;
		size_t length;

		if ((arg != NULL) && (strcmp(arg, k_raceTicksOption) == 0))
		{
			if (seenRaceTicks || (index + 1 >= argc) || (argv[index + 1] == NULL) ||
			    !NativeArcadeLinkAutopilotOptions_ParseRaceTicks(argv[index + 1], &candidate.raceTickLimit))
			{
				return 0;
			}
			seenRaceTicks = 1;
			index++;
			continue;
		}
		if ((arg == NULL) || (strcmp(arg, k_autopilotOption) != 0))
		{
			continue;
		}
		if (seen || (index + 1 >= argc) || (argv[index + 1] == NULL) || (argv[index + 1][0] == '-'))
		{
			return 0;
		}
		value = argv[++index];
		length = strlen(value);
		if ((length == 0u) || (length >= sizeof(candidate.reportPath)))
		{
			return 0;
		}
		memset(candidate.reportPath, 0, sizeof(candidate.reportPath));
		memcpy(candidate.reportPath, value, length);
		candidate.enabled = 1u;
		seen = 1;
	}
	/* The race tick count only lowers the autopilot race's bound: without
	 * the autopilot it is an error. */
	if (seenRaceTicks && !seen)
	{
		return 0;
	}
	*options = candidate;
	return 1;
}

void NativeArcadeLinkAutopilot_Init(struct NativeArcadeLinkAutopilot *autopilot)
{
	if (autopilot == NULL)
	{
		return;
	}
	memset(autopilot, 0, sizeof(*autopilot));
}

/* 1 when a peer holds the character under the local human's cursor. */
static int NativeArcadeLinkAutopilot_LocalCharacterBlocked(const struct NativeArcadeLinkHostSelectView *select)
{
	uint8_t character;

	if (select->localHuman >= NATIVE_ARCADE_LINK_HOST_VIEW_MAX_HUMANS)
	{
		return 0;
	}
	character = select->humans[select->localHuman].characterID;
	if (character >= 16u)
	{
		return 0;
	}
	return (select->peerLockedCharacterMask & (uint16_t)(1u << character)) != 0u;
}

int NativeArcadeLinkAutopilot_Decide(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,
	uint8_t enterReady, struct NativeArcadeLinkAutopilotOutput *output)
{
	uint32_t wantedRow;
	int pressDecision;

	if ((autopilot == NULL) || (view == NULL) || (output == NULL))
	{
		return 0;
	}
	memset(output, 0, sizeof(*output));
	autopilot->confirmedRow = NATIVE_ARCADE_LINK_AUTOPILOT_CONFIRMED_NONE;
	if (autopilot->done != 0u)
	{
		return 1;
	}
	pressDecision = (autopilot->decisions % NATIVE_ARCADE_LINK_AUTOPILOT_PRESS_PERIOD) == 0u;
	if (autopilot->decisions < UINT32_MAX)
	{
		autopilot->decisions++;
	}

	switch (view->screen)
	{
	case NATIVE_ARCADE_FLOW_SCREEN_OFF:
		/* START on the attract screen, once per run. */
		if ((autopilot->sessionStarted == 0u) && (enterReady != 0u))
		{
			output->enter = 1u;
		}
		break;
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT:
		if (pressDecision && (view->select.active != 0u) && (view->select.status == NATIVE_ARCADE_LINK_HOST_SELECT_STATUS_PICKING) &&
			(view->select.currentItem < NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_DONE))
		{
			if ((view->select.currentItem == NATIVE_ARCADE_LINK_HOST_SELECT_ITEM_CHARACTER) &&
				NativeArcadeLinkAutopilot_LocalCharacterBlocked(&view->select))
			{
				output->heldButtons = NATIVE_ARCADE_MENU_BUTTON_DOWN;
			}
			else
			{
				output->heldButtons = NATIVE_ARCADE_MENU_BUTTON_CROSS;
			}
		}
		break;
	case NATIVE_ARCADE_FLOW_SCREEN_RESULTS:
		if (pressDecision && (view->rowsEnabled != 0u) && (view->endReason == NATIVE_ARCADE_FLOW_END_FINISHED))
		{
			wantedRow = (autopilot->racesFinished >= NATIVE_ARCADE_LINK_AUTOPILOT_RACES) ? NATIVE_ARCADE_FLOW_ROW_EXIT
																						  : NATIVE_ARCADE_FLOW_ROW_REMATCH;
			if (view->selectedRow != wantedRow)
			{
				output->heldButtons = NATIVE_ARCADE_MENU_BUTTON_DOWN;
			}
			else
			{
				output->heldButtons = NATIVE_ARCADE_MENU_BUTTON_CROSS;
				autopilot->confirmedRow = (uint8_t)(wantedRow + 1u);
			}
		}
		break;
	default:
		break;
	}
	return 1;
}

int NativeArcadeLinkAutopilot_RecordMatch(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostMatch *match)
{
	struct NativeArcadeLinkAutopilotRace *race;

	if ((autopilot == NULL) || (autopilot->done != 0u))
	{
		return 0;
	}
	if (autopilot->racesStarted >= NATIVE_ARCADE_LINK_AUTOPILOT_RACES)
	{
		NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE);
		return 0;
	}
	if (match == NULL)
	{
		NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);
		return 0;
	}
	race = &autopilot->races[autopilot->racesStarted];
	race->match = *match;
	race->matchRecorded = 1u;
	autopilot->racesStarted++;
	return 1;
}

int NativeArcadeLinkAutopilot_RecordValidated(struct NativeArcadeLinkAutopilot *autopilot, uint32_t validatedTotal, uint32_t launchNumber,
	const uint8_t digests[NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT * NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES])
{
	struct NativeArcadeLinkAutopilotRace *race;

	if ((autopilot == NULL) || (autopilot->done != 0u) || (validatedTotal == autopilot->racesValidated))
	{
		return 0;
	}
	if ((validatedTotal != autopilot->racesValidated + 1u) || (autopilot->racesValidated >= autopilot->racesStarted))
	{
		NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE);
		return 0;
	}
	if (digests == NULL)
	{
		NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);
		return 0;
	}
	race = &autopilot->races[autopilot->racesValidated];
	race->launchNumber = launchNumber;
	memcpy(race->digests, digests, sizeof(race->digests));
	race->validated = 1u;
	autopilot->racesValidated++;
	return 1;
}

int NativeArcadeLinkAutopilot_Observe(struct NativeArcadeLinkAutopilot *autopilot, const struct NativeArcadeLinkHostView *view,
	uint32_t action)
{
	uint32_t previous;
	uint8_t confirmedRow;

	if ((autopilot == NULL) || (autopilot->done != 0u))
	{
		return 0;
	}
	confirmedRow = autopilot->confirmedRow;
	autopilot->confirmedRow = NATIVE_ARCADE_LINK_AUTOPILOT_CONFIRMED_NONE;
	if (autopilot->ticks < UINT32_MAX)
	{
		autopilot->ticks++;
	}

	if (view != NULL)
	{
		previous = autopilot->lastScreen;
		autopilot->lastScreen = view->screen;
		autopilot->lastEndReason = view->endReason;
		autopilot->localCab = view->localCab;
		if (view->screen != NATIVE_ARCADE_FLOW_SCREEN_OFF)
		{
			autopilot->sessionStarted = 1u;
		}

		if ((view->screen == NATIVE_ARCADE_FLOW_SCREEN_RESULTS) && (previous != NATIVE_ARCADE_FLOW_SCREEN_RESULTS))
		{
			if (view->endReason != NATIVE_ARCADE_FLOW_END_FINISHED)
			{
				NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED);
				return 1;
			}
			autopilot->racesFinished++;
			if ((autopilot->racesFinished != autopilot->racesStarted) || (autopilot->racesValidated != autopilot->racesStarted))
			{
				NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING);
				return 1;
			}
		}
		if (previous == NATIVE_ARCADE_FLOW_SCREEN_RESULTS)
		{
			if ((view->screen == NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT) &&
				(confirmedRow == (uint8_t)(NATIVE_ARCADE_FLOW_ROW_REMATCH + 1u)))
			{
				autopilot->rematches++;
			}
			else if ((view->screen == NATIVE_ARCADE_FLOW_SCREEN_EXIT) &&
				(confirmedRow == (uint8_t)(NATIVE_ARCADE_FLOW_ROW_EXIT + 1u)) &&
				(autopilot->racesFinished == NATIVE_ARCADE_LINK_AUTOPILOT_RACES))
			{
				autopilot->exitConfirmed = 1u;
			}
		}
		if ((view->screen == NATIVE_ARCADE_FLOW_SCREEN_EXIT) && (view->endReason == NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT))
		{
			NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST);
			return 1;
		}
	}

	if (action == NATIVE_ARCADE_FLOW_ACTION_RETURN_TO_TITLE)
	{
		if ((autopilot->exitConfirmed != 0u) && (autopilot->racesStarted == NATIVE_ARCADE_LINK_AUTOPILOT_RACES) &&
			(autopilot->racesValidated == NATIVE_ARCADE_LINK_AUTOPILOT_RACES) &&
			(autopilot->racesFinished == NATIVE_ARCADE_LINK_AUTOPILOT_RACES) &&
			(autopilot->rematches == NATIVE_ARCADE_LINK_AUTOPILOT_RACES - 1u))
		{
			autopilot->result = NATIVE_ARCADE_LINK_AUTOPILOT_PASS;
			autopilot->done = 1u;
		}
		else
		{
			NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST);
		}
		return 1;
	}
	if (autopilot->ticks >= NATIVE_ARCADE_LINK_AUTOPILOT_DEADLINE_TICKS)
	{
		NativeArcadeLinkAutopilot_Fail(autopilot, NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT);
		return 1;
	}
	return 0;
}

const char *NativeArcadeLinkAutopilot_ResultName(uint32_t result)
{
	switch (result)
	{
	case NATIVE_ARCADE_LINK_AUTOPILOT_PASS:
		return "PASS";
	case NATIVE_ARCADE_LINK_AUTOPILOT_TIMEOUT:
		return "TIMEOUT";
	case NATIVE_ARCADE_LINK_AUTOPILOT_RACE_FAILED:
		return "RACE_FAILED";
	case NATIVE_ARCADE_LINK_AUTOPILOT_SESSION_LOST:
		return "SESSION_LOST";
	case NATIVE_ARCADE_LINK_AUTOPILOT_UNEXPECTED_RACE:
		return "UNEXPECTED_RACE";
	case NATIVE_ARCADE_LINK_AUTOPILOT_EVIDENCE_MISSING:
		return "EVIDENCE_MISSING";
	case NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_WRITE_FAILED:
		return "REPORT_WRITE_FAILED";
	default:
		return "unknown";
	}
}

const char *NativeArcadeLinkAutopilot_ScreenName(uint32_t screen)
{
	switch (screen)
	{
	case NATIVE_ARCADE_FLOW_SCREEN_OFF:
		return "OFF";
	case NATIVE_ARCADE_FLOW_SCREEN_LOBBY:
		return "LOBBY";
	case NATIVE_ARCADE_FLOW_SCREEN_MATCH_FOUND:
		return "MATCH_FOUND";
	case NATIVE_ARCADE_FLOW_SCREEN_RACING:
		return "RACING";
	case NATIVE_ARCADE_FLOW_SCREEN_RESULTS:
		return "RESULTS";
	case NATIVE_ARCADE_FLOW_SCREEN_REMATCH_WAIT:
		return "REMATCH_WAIT";
	case NATIVE_ARCADE_FLOW_SCREEN_EXIT:
		return "EXIT";
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT:
		return "SELECT";
	case NATIVE_ARCADE_FLOW_SCREEN_SELECT_RESULT:
		return "SELECT_RESULT";
	default:
		return "unknown";
	}
}

const char *NativeArcadeLinkAutopilot_EndReasonName(uint32_t endReason)
{
	switch (endReason)
	{
	case NATIVE_ARCADE_FLOW_END_NONE:
		return "NONE";
	case NATIVE_ARCADE_FLOW_END_FINISHED:
		return "FINISHED";
	case NATIVE_ARCADE_FLOW_END_PEER_TIMEOUT:
		return "PEER_TIMEOUT";
	case NATIVE_ARCADE_FLOW_END_DESYNC:
		return "DESYNC";
	case NATIVE_ARCADE_FLOW_END_LINK_ERROR:
		return "LINK_ERROR";
	case NATIVE_ARCADE_FLOW_END_OPPONENT_LEFT:
		return "OPPONENT_LEFT";
	default:
		return "unknown";
	}
}

/* A bounded text builder: every Append fails once the buffer is full. */
struct NativeArcadeLinkAutopilotText
{
	char *buffer;
	size_t capacity;
	size_t length;
	int ok;
};

static void NativeArcadeLinkAutopilot_Append(struct NativeArcadeLinkAutopilotText *text, const char *format, ...)
{
	va_list args;
	int written;

	if (!text->ok)
	{
		return;
	}
	va_start(args, format);
	written = vsnprintf(text->buffer + text->length, text->capacity - text->length, format, args);
	va_end(args);
	if ((written < 0) || ((size_t)written >= (text->capacity - text->length)))
	{
		text->ok = 0;
		return;
	}
	text->length += (size_t)written;
}

static char NativeArcadeLinkAutopilot_RoleLetter(uint8_t role)
{
	switch (role)
	{
	case NATIVE_ARCADE_LINK_HOST_ROLE_CAB1:
		return '1';
	case NATIVE_ARCADE_LINK_HOST_ROLE_CAB2:
		return '2';
	case NATIVE_ARCADE_LINK_HOST_ROLE_BOT:
		return 'B';
	default:
		return '-';
	}
}

static void NativeArcadeLinkAutopilot_AppendMatch(struct NativeArcadeLinkAutopilotText *text, const struct NativeArcadeLinkHostMatch *match)
{
	char roles[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS + 1u];

	for (uint32_t slot = 0; slot < NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS; slot++)
	{
		roles[slot] = NativeArcadeLinkAutopilot_RoleLetter(match->slotRole[slot]);
	}
	roles[NATIVE_ARCADE_LINK_HOST_MATCH_SLOTS] = '\0';
	NativeArcadeLinkAutopilot_Append(text, "agreed match track %u laps %u seed 0x%08X%08X slots %u %u %u %u %u %u %u %u (%s)",
		(unsigned)match->trackID, (unsigned)match->lapCount, (unsigned)(uint32_t)(match->masterSeed >> 32),
		(unsigned)(uint32_t)(match->masterSeed & 0xFFFFFFFFu), (unsigned)match->slotCharacter[0], (unsigned)match->slotCharacter[1],
		(unsigned)match->slotCharacter[2], (unsigned)match->slotCharacter[3], (unsigned)match->slotCharacter[4],
		(unsigned)match->slotCharacter[5], (unsigned)match->slotCharacter[6], (unsigned)match->slotCharacter[7], roles);
}

int NativeArcadeLinkAutopilot_FormatMatch(const struct NativeArcadeLinkHostMatch *match, char *buffer, size_t capacity, size_t *length)
{
	struct NativeArcadeLinkAutopilotText text;

	if ((match == NULL) || (buffer == NULL) || (capacity == 0u) || (length == NULL))
	{
		if ((buffer != NULL) && (capacity != 0u))
		{
			buffer[0] = '\0';
		}
		return 0;
	}
	text.buffer = buffer;
	text.capacity = capacity;
	text.length = 0u;
	text.ok = 1;
	buffer[0] = '\0';
	NativeArcadeLinkAutopilot_AppendMatch(&text, match);
	if (!text.ok)
	{
		buffer[0] = '\0';
		return 0;
	}
	*length = text.length;
	return 1;
}

int NativeArcadeLinkAutopilot_FormatReport(const struct NativeArcadeLinkAutopilot *autopilot, char *buffer, size_t capacity, size_t *length)
{
	static const char *const digestNames[NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT] = { "config", "plan", "bots", "bank" };
	struct NativeArcadeLinkAutopilotText text;

	if ((autopilot == NULL) || (buffer == NULL) || (capacity == 0u) || (length == NULL))
	{
		if ((buffer != NULL) && (capacity != 0u))
		{
			buffer[0] = '\0';
		}
		return 0;
	}
	text.buffer = buffer;
	text.capacity = capacity;
	text.length = 0u;
	text.ok = 1;
	buffer[0] = '\0';

	NativeArcadeLinkAutopilot_Append(&text, "arcade link autopilot v1\n");
	NativeArcadeLinkAutopilot_Append(&text, "cab %u\n", (unsigned)autopilot->localCab);
	NativeArcadeLinkAutopilot_Append(&text, "result %s (%u)\n", NativeArcadeLinkAutopilot_ResultName(autopilot->result),
		(unsigned)autopilot->result);
	NativeArcadeLinkAutopilot_Append(&text, "last screen %s end reason %s\n", NativeArcadeLinkAutopilot_ScreenName(autopilot->lastScreen),
		NativeArcadeLinkAutopilot_EndReasonName(autopilot->lastEndReason));
	NativeArcadeLinkAutopilot_Append(&text, "ticks %u\n", (unsigned)autopilot->ticks);
	for (uint32_t k = 0; k < NATIVE_ARCADE_LINK_AUTOPILOT_RACES; k++)
	{
		const struct NativeArcadeLinkAutopilotRace *race = &autopilot->races[k];

		if (race->matchRecorded != 0u)
		{
			NativeArcadeLinkAutopilot_Append(&text, "race %u ", (unsigned)(k + 1u));
			NativeArcadeLinkAutopilot_AppendMatch(&text, &race->match);
			NativeArcadeLinkAutopilot_Append(&text, "\n");
		}
		if (race->validated != 0u)
		{
			NativeArcadeLinkAutopilot_Append(&text, "race %u validated launch %u", (unsigned)(k + 1u), (unsigned)race->launchNumber);
			for (uint32_t d = 0; d < NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_COUNT; d++)
			{
				NativeArcadeLinkAutopilot_Append(&text, " %s ", digestNames[d]);
				for (uint32_t i = 0; i < NATIVE_ARCADE_LINK_AUTOPILOT_DIGEST_BYTES; i++)
				{
					NativeArcadeLinkAutopilot_Append(&text, "%02x", (unsigned)race->digests[d][i]);
				}
			}
			NativeArcadeLinkAutopilot_Append(&text, "\n");
		}
	}
	NativeArcadeLinkAutopilot_Append(&text, "end races %u\n", (unsigned)autopilot->racesValidated);
	if (!text.ok)
	{
		buffer[0] = '\0';
		return 0;
	}
	*length = text.length;
	return 1;
}

int NativeArcadeLinkAutopilot_WriteReport(const char *path, const struct NativeArcadeLinkAutopilot *autopilot)
{
	char text[NATIVE_ARCADE_LINK_AUTOPILOT_REPORT_BYTES];
	size_t length = 0u;
	FILE *file;
	int ok;

	if ((path == NULL) || (path[0] == '\0') || !NativeArcadeLinkAutopilot_FormatReport(autopilot, text, sizeof(text), &length))
	{
		return 0;
	}
	file = fopen(path, "wb");
	if (file == NULL)
	{
		return 0;
	}
	ok = (fwrite(text, 1u, length, file) == length);
	ok = (fclose(file) == 0) && ok;
	return ok;
}

/* Steering (LR-16). Differences are clamped to +-2^30 so every product and
 * sum below fits in 64 bits. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_DELTA_LIMIT (INT64_C(1) << 30)
/* 45 degrees in angle units, and the arctangent's correction term
 * (0.273 rad in angle units): atan(t) ~ pi/4 t + 0.273 t (1 - t) on [0, 1]. */
#define NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_EIGHTH 512u
#define NATIVE_ARCADE_LINK_AUTOPILOT_ATAN_CORRECTION 178u
#define NATIVE_ARCADE_LINK_AUTOPILOT_Q12 4096u

static int64_t NativeArcadeLinkAutopilot_Delta(int32_t to, int32_t from)
{
	int64_t delta = (int64_t)to - (int64_t)from;

	if (delta > NATIVE_ARCADE_LINK_AUTOPILOT_DELTA_LIMIT)
	{
		return NATIVE_ARCADE_LINK_AUTOPILOT_DELTA_LIMIT;
	}
	if (delta < -NATIVE_ARCADE_LINK_AUTOPILOT_DELTA_LIMIT)
	{
		return -NATIVE_ARCADE_LINK_AUTOPILOT_DELTA_LIMIT;
	}
	return delta;
}

/* atan(small / large) in angle units, 0..512, for 0 <= small <= large, large > 0. */
static uint32_t NativeArcadeLinkAutopilot_AtanUnit(uint64_t small, uint64_t large)
{
	const uint64_t t = (small * NATIVE_ARCADE_LINK_AUTOPILOT_Q12) / large;
	const uint64_t linear = NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_EIGHTH * t;
	const uint64_t correction =
		(NATIVE_ARCADE_LINK_AUTOPILOT_ATAN_CORRECTION * t * (NATIVE_ARCADE_LINK_AUTOPILOT_Q12 - t)) / NATIVE_ARCADE_LINK_AUTOPILOT_Q12;

	return (uint32_t)((linear + correction + (NATIVE_ARCADE_LINK_AUTOPILOT_Q12 / 2u)) / NATIVE_ARCADE_LINK_AUTOPILOT_Q12);
}

int32_t NativeArcadeLinkAutopilot_Angle(int32_t dx, int32_t dz)
{
	const uint64_t ax = (dx < 0) ? (uint64_t)(-(int64_t)dx) : (uint64_t)dx;
	const uint64_t az = (dz < 0) ? (uint64_t)(-(int64_t)dz) : (uint64_t)dz;
	uint32_t angle;

	if ((ax == 0u) && (az == 0u))
	{
		return 0;
	}
	/* The angle from +z toward +x in the first quadrant, 0..1024. */
	if (ax <= az)
	{
		angle = NativeArcadeLinkAutopilot_AtanUnit(ax, az);
	}
	else
	{
		angle = (2u * NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_EIGHTH) - NativeArcadeLinkAutopilot_AtanUnit(az, ax);
	}
	if (dz < 0)
	{
		angle = (4u * NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_EIGHTH) - angle;
	}
	if (dx < 0)
	{
		angle = (uint32_t)NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - angle;
	}
	return (int32_t)(angle & ((uint32_t)NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - 1u));
}

uint32_t NativeArcadeLinkAutopilot_Steer(const struct NativeArcadeLinkAutopilotSteerFacts *facts)
{
	const uint32_t mask = (uint32_t)NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS - 1u;
	int64_t dx;
	int64_t dz;
	uint32_t wrapped;
	int32_t error;

	if (facts == NULL)
	{
		return 0u;
	}
	dx = NativeArcadeLinkAutopilot_Delta(facts->aimX, facts->kartX);
	dz = NativeArcadeLinkAutopilot_Delta(facts->aimZ, facts->kartZ);
	if ((dx == 0) && (dz == 0))
	{
		return NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS;
	}
	wrapped = ((uint32_t)NativeArcadeLinkAutopilot_Angle((int32_t)dx, (int32_t)dz) - (uint32_t)facts->heading) & mask;
	error = (int32_t)wrapped;
	if (error >= (NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS / 2))
	{
		error -= NATIVE_ARCADE_LINK_AUTOPILOT_ANGLE_UNITS;
	}
	if (error > NATIVE_ARCADE_LINK_AUTOPILOT_STEER_DEADBAND)
	{
		return NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS | NATIVE_ARCADE_LINK_AUTOPILOT_STEER_POSITIVE;
	}
	if (error < -NATIVE_ARCADE_LINK_AUTOPILOT_STEER_DEADBAND)
	{
		return NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS | NATIVE_ARCADE_LINK_AUTOPILOT_STEER_NEGATIVE;
	}
	return NATIVE_ARCADE_LINK_AUTOPILOT_BUTTON_CROSS;
}

int NativeArcadeLinkAutopilot_Passed(const struct NativeArcadeLinkAutopilotPassFacts *facts)
{
	int64_t kx;
	int64_t kz;
	int64_t ax;
	int64_t az;
	const int64_t radius = NATIVE_ARCADE_LINK_AUTOPILOT_PASS_RADIUS;

	if (facts == NULL)
	{
		return 0;
	}
	kx = NativeArcadeLinkAutopilot_Delta(facts->kartX, facts->pointX);
	kz = NativeArcadeLinkAutopilot_Delta(facts->kartZ, facts->pointZ);
	if (((kx * kx) + (kz * kz)) <= (radius * radius))
	{
		return 1;
	}
	ax = NativeArcadeLinkAutopilot_Delta(facts->pointX, facts->previousX);
	az = NativeArcadeLinkAutopilot_Delta(facts->pointZ, facts->previousZ);
	return ((kx * ax) + (kz * az)) > 0;
}
