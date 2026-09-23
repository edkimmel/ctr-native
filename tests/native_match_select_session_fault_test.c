#include "platform/native_match_select_session.h"
#include "platform/native_virtual_datagram.h"

#include "platform/native_match_config.h"
#include "platform/native_match_select_message.h"
#include "platform/native_match_select_rules.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

/*
 * Fault injection for the match-select session over the virtual datagram
 * harness. Every human is one session; every pair of humans shares one
 * two-endpoint harness (the lower human is endpoint 0). Each tick:
 *   1. every session receives everything due on each of its harnesses and
 *      Accepts it;
 *   2. every session applies its scripted inputs for the tick, Ticks, and
 *      Composes once (FAILED never composes; CONFIRMED composes for its
 *      linger), sending the record to every peer on a route drawn from a
 *      fixed-seed PRNG: drop, deliver after 1 + 0..maxExtraDelay steps, or
 *      duplicate with two independent delays. So loss, delay, reordering, and
 *      duplication all happen, reproducibly.
 * A record sent at tick t is due at t + 1 at the earliest, so two sessions
 * acting on the same tick never see each other's action that tick.
 */

#define MAX_HUMANS NATIVE_MATCH_SELECT_MAX_HUMANS
#define PAIR_COUNT 6u
#define QUEUE_CAPACITY 64u
#define RECORD_BYTES NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES
#define SERIAL_LIMIT 8192u
#define TICK_LIMIT 1000u
#define NO_TICK UINT32_MAX
#define RESULT_HOLD_TICKS 60u /* SEL-8: the linger after CONFIRMED */
#define ITEM_TICKS 30u
#define FIXTURE_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */

#define IN_NEXT NATIVE_MATCH_SELECT_INPUT_NEXT
#define IN_CONFIRM NATIVE_MATCH_SELECT_INPUT_CONFIRM

struct Direction
{
	uint32_t lossPercent;
	uint32_t duplicatePercent;
	uint32_t maxExtraDelay; /* extra steps beyond the next tick, 0..3 */
	uint32_t corruptEvery;  /* 0: never; N: every Nth record on this direction gets one byte flipped */
	uint32_t cutoffTick;    /* from this tick on every record is dropped; NO_TICK: never */
};

struct ScriptStep
{
	uint32_t tick;
	enum NativeMatchSelectInput input;
};

struct Script
{
	const struct ScriptStep *steps;
	uint32_t count;
};

struct Net
{
	uint32_t humanCount;
	uint32_t tick;
	uint64_t rng;
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession sessions[MAX_HUMANS];
	struct NativeVirtualDatagramPair pairs[PAIR_COUNT];
	struct NativeVirtualDatagramSlot slots[PAIR_COUNT][QUEUE_CAPACITY];
	uint8_t storage[PAIR_COUNT][QUEUE_CAPACITY][RECORD_BYTES];
	struct Direction directions[MAX_HUMANS][MAX_HUMANS]; /* [from][to] */
	struct Script scripts[MAX_HUMANS];
	uint32_t linger[MAX_HUMANS];
	uint32_t lingerSent[MAX_HUMANS];
	uint32_t confirmedTick[MAX_HUMANS];
	uint32_t failedTick[MAX_HUMANS];
	uint32_t lastOkTick[MAX_HUMANS][MAX_HUMANS]; /* [receiver][sender] */
	uint32_t sendCount[MAX_HUMANS][MAX_HUMANS];
	uint8_t corrupted[PAIR_COUNT][SERIAL_LIMIT / 8u];
	uint32_t results[MAX_HUMANS][8];
	uint32_t corruptedMalformed[MAX_HUMANS];
	uint32_t dropped;
	uint32_t duplicated;
	uint32_t delayed;
	uint32_t corruptedSent;
	int haveFirst[MAX_HUMANS];
	uint8_t firstComposed[MAX_HUMANS][RECORD_BYTES];
	uint8_t lastComposed[MAX_HUMANS][RECORD_BYTES];
};

static struct Net g_net;

static void FillCounting(uint8_t *bytes, size_t size, uint8_t start)
{
	for (size_t i = 0; i < size; i++)
	{
		bytes[i] = (uint8_t)(start + i);
	}
}

/* The native_match_select_rules_test fixture: two cabinets, characters 0..5. */
static void BuildTwoCabBase(struct NativeMatchConfigV1 *config)
{
	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeTwoCab(config);
	config->trackID = 3;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = FIXTURE_SEED;
	FillCounting(config->buildIdentity, sizeof(config->buildIdentity), 0x40u);
	FillCounting(config->contentIdentity, sizeof(config->contentIdentity), 0x80u);
	FillCounting(config->botRulesDigest, sizeof(config->botRulesDigest), 0xc0u);
	for (uint32_t i = 0; i <= 5u; i++)
	{
		config->slots[i].characterID = (uint8_t)i;
		config->slots[i].difficulty = 0;
	}
}

/* splitmix64: a small deterministic PRNG owned by the test. */
static uint64_t NextRandom(struct Net *net)
{
	uint64_t z = (net->rng += UINT64_C(0x9e3779b97f4a7c15));

	z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
	z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
	return z ^ (z >> 31);
}

static uint32_t PairIndex(uint32_t a, uint32_t b)
{
	const uint32_t lo = (a < b) ? a : b;
	const uint32_t hi = (a < b) ? b : a;

	return ((lo * ((2u * MAX_HUMANS) - lo - 1u)) / 2u) + (hi - lo - 1u);
}

static uint32_t Endpoint(uint32_t self, uint32_t other)
{
	return (self < other) ? 0u : 1u;
}

static int NetInit(struct Net *net, uint32_t humanCount, const uint64_t nonces[], const uint8_t characters[],
	const uint8_t tracks[], const uint8_t laps[], uint32_t itemTicks, uint64_t seed)
{
	struct NativeMatchSelectTimings timings;

	memset(net, 0, sizeof(*net));
	net->humanCount = humanCount;
	net->rng = seed;
	BuildTwoCabBase(&net->base);
	NativeMatchSelectSession_DefaultTimings(&timings);
	timings.characterTicks = itemTicks;
	timings.trackTicks = itemTicks;
	timings.lapTicks = itemTicks;
	for (uint32_t h = 0; h < humanCount; h++)
	{
		CHECK(NativeMatchSelectSession_Init(&net->sessions[h], &net->base, humanCount, h, nonces[h], characters[h], tracks[h],
			laps[h], &timings));
		net->linger[h] = RESULT_HOLD_TICKS;
		net->confirmedTick[h] = NO_TICK;
		net->failedTick[h] = NO_TICK;
		for (uint32_t p = 0; p < MAX_HUMANS; p++)
		{
			net->lastOkTick[h][p] = NO_TICK;
			net->directions[h][p].cutoffTick = NO_TICK;
		}
	}
	for (uint32_t i = 0; i < humanCount; i++)
	{
		for (uint32_t j = i + 1u; j < humanCount; j++)
		{
			const uint32_t index = PairIndex(i, j);

			CHECK(NativeVirtualDatagramPair_Init(&net->pairs[index], net->slots[index], QUEUE_CAPACITY, &net->storage[index][0][0],
				RECORD_BYTES));
		}
	}
	return 0;
}

static void SetScript(struct Net *net, uint32_t human, const struct ScriptStep *steps, uint32_t count)
{
	net->scripts[human].steps = steps;
	net->scripts[human].count = count;
}

static uint32_t LastConfirmedTick(const struct Net *net)
{
	uint32_t last = 0;

	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		if ((net->confirmedTick[h] != NO_TICK) && (net->confirmedTick[h] > last))
		{
			last = net->confirmedTick[h];
		}
	}
	return last;
}

static void SetAllDirections(struct Net *net, uint32_t lossPercent, uint32_t duplicatePercent, uint32_t maxExtraDelay)
{
	for (uint32_t from = 0; from < net->humanCount; from++)
	{
		for (uint32_t to = 0; to < net->humanCount; to++)
		{
			net->directions[from][to].lossPercent = lossPercent;
			net->directions[from][to].duplicatePercent = duplicatePercent;
			net->directions[from][to].maxExtraDelay = maxExtraDelay;
		}
	}
}

static void NoteStatus(struct Net *net, uint32_t h)
{
	const uint32_t status = NativeMatchSelectSession_Status(&net->sessions[h]);

	if ((status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED) && (net->confirmedTick[h] == NO_TICK))
	{
		net->confirmedTick[h] = net->tick;
	}
	if ((status == NATIVE_MATCH_SELECT_STATUS_FAILED) && (net->failedTick[h] == NO_TICK))
	{
		net->failedTick[h] = net->tick;
	}
}

static int IsCorrupted(const struct Net *net, uint32_t pairIndex, uint64_t serial)
{
	return (serial < SERIAL_LIMIT) && ((net->corrupted[pairIndex][serial / 8u] & (1u << (serial % 8u))) != 0);
}

static uint64_t DeliveryStep(struct Net *net, const struct Direction *direction)
{
	const uint64_t extra = NextRandom(net) % (uint64_t)(direction->maxExtraDelay + 1u);

	if (extra != 0)
	{
		net->delayed++;
	}
	return (uint64_t)net->tick + 1u + extra;
}

static int SendTo(struct Net *net, uint32_t from, uint32_t to, const uint8_t record[RECORD_BYTES])
{
	const struct Direction *direction = &net->directions[from][to];
	const uint32_t pairIndex = PairIndex(from, to);
	struct NativeVirtualDatagramPair *pair = &net->pairs[pairIndex];
	struct NativeVirtualDatagramRoute route;
	uint8_t copy[RECORD_BYTES];
	int corrupt = 0;

	memcpy(copy, record, sizeof(copy));
	net->sendCount[from][to]++;
	if ((direction->corruptEvery != 0) && ((net->sendCount[from][to] % direction->corruptEvery) == 0))
	{
		const uint32_t at = (uint32_t)(NextRandom(net) % RECORD_BYTES);

		copy[at] ^= (uint8_t)(1u + (NextRandom(net) % 255u));
		corrupt = 1;
	}

	memset(&route, 0, sizeof(route));
	if (net->tick >= direction->cutoffTick)
	{
		route.action = NATIVE_VIRTUAL_DATAGRAM_DROP;
	}
	else
	{
		const uint32_t roll = (uint32_t)(NextRandom(net) % 100u);

		if (roll < direction->lossPercent)
		{
			route.action = NATIVE_VIRTUAL_DATAGRAM_DROP;
		}
		else if (roll < direction->lossPercent + direction->duplicatePercent)
		{
			route.action = NATIVE_VIRTUAL_DATAGRAM_DUPLICATE;
			route.firstDeliveryStep = DeliveryStep(net, direction);
			route.secondDeliveryStep = DeliveryStep(net, direction);
		}
		else
		{
			route.action = NATIVE_VIRTUAL_DATAGRAM_DELIVER;
			route.firstDeliveryStep = DeliveryStep(net, direction);
		}
	}
	if (route.action == NATIVE_VIRTUAL_DATAGRAM_DROP)
	{
		net->dropped++;
	}
	else if (route.action == NATIVE_VIRTUAL_DATAGRAM_DUPLICATE)
	{
		net->duplicated++;
	}

	CHECK(NativeVirtualDatagramPair_Send(pair, Endpoint(from, to), copy, sizeof(copy), &route));
	if (corrupt)
	{
		CHECK(pair->nextSerial < SERIAL_LIMIT);
		net->corrupted[pairIndex][pair->nextSerial / 8u] |= (uint8_t)(1u << (pair->nextSerial % 8u));
		net->corruptedSent++;
	}
	return 0;
}

static int ReceiveAll(struct Net *net, uint32_t self)
{
	for (uint32_t peer = 0; peer < net->humanCount; peer++)
	{
		const uint32_t pairIndex = PairIndex(self, peer);

		if (peer == self)
		{
			continue;
		}
		for (;;)
		{
			uint8_t bytes[RECORD_BYTES * 2u];
			size_t size = sizeof(bytes);
			struct NativeVirtualDatagramMetadata metadata;
			enum NativeVirtualDatagramReceiveResult received;
			enum NativeMatchSelectAcceptResult result;

			received = NativeVirtualDatagramPair_Receive(&net->pairs[pairIndex], Endpoint(self, peer), bytes, &size, &metadata);
			if (received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_EMPTY)
			{
				break;
			}
			CHECK(received == NATIVE_VIRTUAL_DATAGRAM_RECEIVE_OK);
			result = NativeMatchSelectSession_Accept(&net->sessions[self], bytes, size);
			CHECK((uint32_t)result < 8u);
			net->results[self][result]++;
			if (IsCorrupted(net, pairIndex, metadata.serial))
			{
				CHECK(result == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED || result == NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL);
				if (result == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED)
				{
					net->corruptedMalformed[self]++;
				}
			}
			else
			{
				CHECK(result != NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED);
			}
			if (result == NATIVE_MATCH_SELECT_ACCEPT_OK)
			{
				net->lastOkTick[self][peer] = net->tick;
			}
		}
	}
	return 0;
}

static int NetStep(struct Net *net)
{
	for (uint32_t i = 0; i < net->humanCount; i++)
	{
		for (uint32_t j = i + 1u; j < net->humanCount; j++)
		{
			CHECK(NativeVirtualDatagramPair_AdvanceTo(&net->pairs[PairIndex(i, j)], net->tick));
		}
	}

	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		CHECK(ReceiveAll(net, h) == 0);
		NoteStatus(net, h);
	}

	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		struct NativeMatchSelectSession *session = &net->sessions[h];
		uint8_t record[RECORD_BYTES];
		size_t size = 0;
		uint32_t status;

		for (uint32_t i = 0; i < net->scripts[h].count; i++)
		{
			if (net->scripts[h].steps[i].tick == net->tick)
			{
				(void)NativeMatchSelectSession_ApplyInput(session, net->scripts[h].steps[i].input);
			}
		}
		NativeMatchSelectSession_Tick(session);
		NoteStatus(net, h);

		status = NativeMatchSelectSession_Status(session);
		if (status == NATIVE_MATCH_SELECT_STATUS_FAILED)
		{
			CHECK(!NativeMatchSelectSession_Compose(session, record, sizeof(record), &size));
			continue;
		}
		if (status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED)
		{
			if (net->lingerSent[h] >= net->linger[h])
			{
				continue;
			}
			net->lingerSent[h]++;
		}
		CHECK(NativeMatchSelectSession_Compose(session, record, sizeof(record), &size));
		CHECK(size == RECORD_BYTES);
		if (!net->haveFirst[h])
		{
			memcpy(net->firstComposed[h], record, sizeof(record));
			net->haveFirst[h] = 1;
		}
		memcpy(net->lastComposed[h], record, sizeof(record));
		for (uint32_t peer = 0; peer < net->humanCount; peer++)
		{
			if (peer != h)
			{
				CHECK(SendTo(net, h, peer, record) == 0);
			}
		}
	}

	net->tick++;
	return 0;
}

static int Settled(const struct Net *net)
{
	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		const uint32_t status = NativeMatchSelectSession_Status(&net->sessions[h]);

		if ((status != NATIVE_MATCH_SELECT_STATUS_CONFIRMED) && (status != NATIVE_MATCH_SELECT_STATUS_FAILED))
		{
			return 0;
		}
		if ((status == NATIVE_MATCH_SELECT_STATUS_CONFIRMED) && (net->lingerSent[h] < net->linger[h]))
		{
			return 0;
		}
	}
	return 1;
}

static int RunUntilSettled(struct Net *net)
{
	while (!Settled(net))
	{
		CHECK(net->tick < TICK_LIMIT);
		CHECK(NetStep(net) == 0);
	}
	return 0;
}

static int RunTo(struct Net *net, uint32_t tick)
{
	while (net->tick < tick)
	{
		CHECK(NetStep(net) == 0);
	}
	return 0;
}

/* The safety invariant: two CONFIRMED sessions never hold different outcomes. */
static int CheckNoWrongConfirm(const struct Net *net)
{
	for (uint32_t i = 0; i < net->humanCount; i++)
	{
		for (uint32_t j = i + 1u; j < net->humanCount; j++)
		{
			if ((NativeMatchSelectSession_Status(&net->sessions[i]) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED) &&
			    (NativeMatchSelectSession_Status(&net->sessions[j]) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED))
			{
				CHECK(memcmp(NativeMatchSelectSession_Outcome(&net->sessions[i]), NativeMatchSelectSession_Outcome(&net->sessions[j]),
					sizeof(struct NativeMatchSelectOutcome)) == 0);
				CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(&net->sessions[i]),
					NativeMatchSelectSession_ResolvedDigest(&net->sessions[j]), NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) == 0);
			}
		}
	}
	return 0;
}

/*
 * Every session CONFIRMED with byte-identical outcomes and resolved digests,
 * equal to a direct Resolve of every human's final locked choice; for two
 * humans, byte-identical built configs.
 */
static int CheckAllConfirmedIdentical(const struct Net *net, struct NativeMatchSelectOutcome *outcomeOut)
{
	struct NativeMatchSelectChoice choices[MAX_HUMANS];
	struct NativeMatchSelectOutcome expected;
	struct NativeMatchConfigV1 firstConfig;

	memset(&firstConfig, 0, sizeof(firstConfig));

	memset(choices, 0, sizeof(choices));
	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		const struct NativeMatchSelectHumanState *local = NativeMatchSelectSession_Human(&net->sessions[h], h);

		CHECK(NativeMatchSelectSession_Status(&net->sessions[h]) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
		CHECK(NativeMatchSelectSession_Fault(&net->sessions[h]) == NATIVE_MATCH_SELECT_SESSION_FAULT_NONE);
		CHECK(local != NULL && local->lockMask == 0x7u);
		choices[h].characterID = local->characterID;
		choices[h].trackID = local->trackID;
		choices[h].lapCount = local->lapCount;
		choices[h].nonce = local->nonce;
	}
	CHECK(NativeMatchSelect_Resolve(&net->base, net->humanCount, choices, &expected));
	for (uint32_t h = 0; h < net->humanCount; h++)
	{
		const struct NativeMatchSelectSession *session = &net->sessions[h];

		CHECK(NativeMatchSelectSession_Outcome(session) != NULL);
		CHECK(memcmp(NativeMatchSelectSession_Outcome(session), &expected, sizeof(expected)) == 0);
		CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(session), NativeMatchSelectSession_ResolvedDigest(&net->sessions[0]),
			NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES) == 0);
		if (net->humanCount == 2u)
		{
			struct NativeMatchConfigV1 config;

			memset(&config, 0, sizeof(config));
			CHECK(NativeMatchSelect_BuildConfig(&net->base, NativeMatchSelectSession_Outcome(session), &config));
			CHECK(NativeMatchConfigV1_Validate(&config));
			if (h == 0)
			{
				firstConfig = config;
			}
			else
			{
				CHECK(memcmp(&config, &firstConfig, sizeof(config)) == 0);
			}
		}
	}
	CHECK(CheckNoWrongConfirm(net) == 0);
	if (outcomeOut != NULL)
	{
		*outcomeOut = expected;
	}
	return 0;
}

static const uint64_t k_nonces[MAX_HUMANS] = {
	UINT64_C(0x0123456789abcdef), UINT64_C(0xfedcba9876543210), UINT64_C(0x0f1e2d3c4b5a6978), UINT64_C(0x8796a5b4c3d2e1f0),
};

/* 1. Two humans, clean link, agreeing picks: CONFIRMED, no draw. */
static const struct ScriptStep k_quickA[] = { { 2, IN_CONFIRM }, { 3, IN_CONFIRM }, { 4, IN_CONFIRM } };
static const struct ScriptStep k_quickB[] = { { 5, IN_CONFIRM }, { 6, IN_CONFIRM }, { 7, IN_CONFIRM } };

static int ScenarioCleanAgreement(void)
{
	struct Net *net = &g_net;
	struct NativeMatchSelectOutcome outcome;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };

	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 1u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
	CHECK(outcome.trackID == 3 && outcome.lapCount == 3 && outcome.trackDrawn == 0 && outcome.lapsDrawn == 0);
	CHECK(outcome.humanCharacter[0] == 0 && outcome.humanCharacter[1] == 1 && outcome.characterReassignedMask == 0);
	for (uint32_t h = 0; h < 2u; h++)
	{
		CHECK(NativeMatchSelectSession_DroppedMalformed(&net->sessions[h]) == 0);
		CHECK(NativeMatchSelectSession_DroppedStale(&net->sessions[h]) == 0);
		CHECK(NativeMatchSelectSession_DroppedForeign(&net->sessions[h]) == 0);
		CHECK(NativeMatchSelectSession_DroppedSelf(&net->sessions[h]) == 0);
	}
	printf("scenario 1 clean agreement: CONFIRMED at %u/%u, track %u laps %u\n", net->confirmedTick[0], net->confirmedTick[1],
		outcome.trackID, outcome.lapCount);
	return 0;
}

/* 2. 25% loss + duplication + reorder, disagreeing track and laps: identical draws. */
static const struct ScriptStep k_voteA[] = { { 2, IN_CONFIRM }, { 4, IN_CONFIRM }, { 6, IN_CONFIRM } };
static const struct ScriptStep k_voteB[] = {
	{ 3, IN_CONFIRM }, { 5, IN_NEXT }, { 6, IN_CONFIRM }, { 8, IN_NEXT }, { 9, IN_CONFIRM },
};

static int ScenarioLossyDisagreement(void)
{
	struct Net *net = &g_net;
	struct NativeMatchSelectOutcome outcome;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };
	uint32_t stale = 0;

	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 2u) == 0);
	SetAllDirections(net, 25, 15, 3);
	SetScript(net, 0, k_voteA, 3);
	SetScript(net, 1, k_voteB, 5);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 1)->trackID == 6);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 1)->lapCount == 5);
	CHECK(outcome.trackDrawn == 1 && (outcome.trackID == 3 || outcome.trackID == 6));
	CHECK(outcome.lapsDrawn == 1 && (outcome.lapCount == 3 || outcome.lapCount == 5));
	/* Every fault actually happened. */
	for (uint32_t h = 0; h < 2u; h++)
	{
		stale += NativeMatchSelectSession_DroppedStale(&net->sessions[h]);
	}
	CHECK(net->dropped > 0 && net->duplicated > 0 && net->delayed > 0 && stale > 0);
	printf("scenario 2 lossy disagreement: CONFIRMED at %u/%u, drawn track %u laps %u (dropped %u, duplicated %u, delayed %u, stale %u)\n",
		net->confirmedTick[0], net->confirmedTick[1], outcome.trackID, outcome.lapCount, net->dropped, net->duplicated, net->delayed,
		stale);
	return 0;
}

/* 3. An idle cabinet auto-picks at expiry (SEL-6: its cursor, or the next free character). */
static const struct ScriptStep k_activeA[] = { { 2, IN_CONFIRM }, { 3, IN_CONFIRM }, { 4, IN_CONFIRM } };

static int ScenarioIdleCabinet(void)
{
	struct Net *net = &g_net;
	struct NativeMatchSelectOutcome outcome;
	const uint8_t characters[2] = { 1, 1 };
	const uint8_t tracks[2] = { 9, 9 };
	const uint8_t laps[2] = { 5, 5 };

	/* (a) B idles on the character A locked: it gets the next free one, 2. */
	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 3u) == 0);
	SetAllDirections(net, 10, 5, 2);
	SetScript(net, 0, k_activeA, 3);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 1)->characterID == 2);
	CHECK(outcome.humanCharacter[0] == 1 && outcome.humanCharacter[1] == 2 && outcome.characterReassignedMask == 0);
	CHECK(outcome.trackID == 9 && outcome.lapCount == 5 && outcome.trackDrawn == 0 && outcome.lapsDrawn == 0);
	CHECK(net->confirmedTick[1] >= 3u * ITEM_TICKS - 1u);
	printf("scenario 3a idle cabinet: CONFIRMED at %u/%u, idle pick %u\n", net->confirmedTick[0], net->confirmedTick[1],
		outcome.humanCharacter[1]);

	/* (b) Both idle: every item expires on both, each locks its cursor. */
	{
		const uint8_t distinct[2] = { 3, 6 };

		CHECK(NetInit(net, 2, k_nonces, distinct, tracks, laps, ITEM_TICKS, 4u) == 0);
		SetAllDirections(net, 10, 5, 2);
		CHECK(RunUntilSettled(net) == 0);
		CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
		CHECK(outcome.humanCharacter[0] == 3 && outcome.humanCharacter[1] == 6 && outcome.trackID == 9 && outcome.lapCount == 5);
		printf("scenario 3b both idle: CONFIRMED at %u/%u\n", net->confirmedTick[0], net->confirmedTick[1]);
	}
	return 0;
}

/* 4. Both lock the same character on the same tick: CAB2 is reassigned. */
static const struct ScriptStep k_sameTick[] = { { 5, IN_CONFIRM }, { 6, IN_CONFIRM }, { 7, IN_CONFIRM } };

static int ScenarioSimultaneousCharacter(void)
{
	struct Net *net = &g_net;
	struct NativeMatchSelectOutcome outcome;
	const uint8_t characters[2] = { 2, 2 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };

	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 5u) == 0);
	SetScript(net, 0, k_sameTick, 3);
	SetScript(net, 1, k_sameTick, 3);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
	/* Both really locked 2 (neither CONFIRM was refused). */
	CHECK(NativeMatchSelectSession_Human(&net->sessions[0], 0)->characterID == 2);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 1)->characterID == 2);
	CHECK(outcome.characterReassignedMask == 0x2u);
	CHECK(outcome.humanCharacter[0] == 2 && outcome.humanCharacter[1] == 0);
	printf("scenario 4 simultaneous character: CONFIRMED at %u/%u, CAB2 reassigned to %u (mask 0x%x)\n", net->confirmedTick[0],
		net->confirmedTick[1], outcome.humanCharacter[1], outcome.characterReassignedMask);
	return 0;
}

/* 5. Corrupted datagrams are counted as malformed; the select still confirms. */
static int ScenarioCorruption(void)
{
	struct Net *net = &g_net;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };
	uint32_t malformed = 0;

	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 6u) == 0);
	SetAllDirections(net, 5, 5, 2);
	net->directions[0][1].corruptEvery = 3;
	net->directions[1][0].corruptEvery = 2;
	SetScript(net, 0, k_voteA, 3);
	SetScript(net, 1, k_voteB, 5);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, NULL) == 0);
	for (uint32_t h = 0; h < 2u; h++)
	{
		CHECK(NativeMatchSelectSession_DroppedMalformed(&net->sessions[h]) > 0);
		CHECK(NativeMatchSelectSession_DroppedMalformed(&net->sessions[h]) == net->corruptedMalformed[h]);
		malformed += net->corruptedMalformed[h];
	}
	printf("scenario 5 corruption: CONFIRMED at %u/%u, %u corrupted sent, %u counted malformed\n", net->confirmedTick[0],
		net->confirmedTick[1], net->corruptedSent, malformed);
	return 0;
}

/* 6. Everything from B is lost from tick 40: A fails by silence, exactly peerSilenceTicks after B's last record. */
static int ScenarioSilence(void)
{
	struct Net *net = &g_net;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };
	const uint32_t cutoff = 40u;

	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 7u) == 0);
	SetAllDirections(net, 10, 0, 3);
	net->directions[1][0].cutoffTick = cutoff;
	SetScript(net, 0, k_quickA, 3);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckNoWrongConfirm(net) == 0);

	CHECK(NativeMatchSelectSession_Status(&net->sessions[0]) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&net->sessions[0]) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	CHECK(net->lastOkTick[0][1] != NO_TICK && net->lastOkTick[0][1] <= cutoff + 3u);
	/* Accepted at tick r (silence 0, Tick makes it 1): the 90th Tick is at r + 89. */
	CHECK(net->failedTick[0] == net->lastOkTick[0][1] + NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS - 1u);
	/* A FAILED stops composing, so B then fails the same way: nobody hangs. */
	CHECK(NativeMatchSelectSession_Status(&net->sessions[1]) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&net->sessions[1]) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	CHECK(net->failedTick[1] == net->lastOkTick[1][0] + NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS - 1u);
	printf("scenario 6 silence: A last heard B at %u, FAILED/PEER_SILENT at %u; B FAILED/PEER_SILENT at %u\n", net->lastOkTick[0][1],
		net->failedTick[0], net->failedTick[1]);
	return 0;
}

/* 7. The linger after CONFIRMED. A finishes first; B finishes by expiry, resolves, and A confirms on B's RESOLVED. */
static const struct ScriptStep k_earlyA[] = { { 1, IN_CONFIRM }, { 2, IN_CONFIRM }, { 3, IN_CONFIRM } };

static int ScenarioLinger(void)
{
	struct Net *net = &g_net;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 6 };
	const uint8_t laps[2] = { 3, 5 };

	/* (a) A stops sending the moment it confirms, before B ever saw A RESOLVED: B fails by silence, never hangs. */
	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, 20u, 8u) == 0);
	SetScript(net, 0, k_earlyA, 3);
	net->linger[0] = 0;
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckNoWrongConfirm(net) == 0);
	CHECK(NativeMatchSelectSession_Status(&net->sessions[0]) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(net->lingerSent[0] == 0);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 0)->phase == NATIVE_MATCH_SELECT_PHASE_PICKING);
	CHECK(NativeMatchSelectSession_Status(&net->sessions[1]) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&net->sessions[1]) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	CHECK(net->failedTick[1] == net->lastOkTick[1][0] + NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS - 1u);
	printf("scenario 7a no linger: A CONFIRMED at %u, B (RESOLVED, never saw A RESOLVED) FAILED/PEER_SILENT at %u\n",
		net->confirmedTick[0], net->failedTick[1]);

	/* (b) A keeps sending for 60 ticks under 50% loss: B confirms. */
	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, 20u, 9u) == 0);
	SetAllDirections(net, 50, 0, 1);
	SetScript(net, 0, k_earlyA, 3);
	net->linger[0] = RESULT_HOLD_TICKS;
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, NULL) == 0);
	CHECK(net->confirmedTick[1] > net->confirmedTick[0]);
	printf("scenario 7b 60-tick linger at 50%% loss: A CONFIRMED at %u, B CONFIRMED at %u\n", net->confirmedTick[0],
		net->confirmedTick[1]);
	return 0;
}

/*
 * 8. A stale record from an earlier select on the SAME base (so the same
 * baseDigest; a different nonce) reaches B. Documented behaviour, per case:
 * (a) before any fresh record, a PICKING record with sequence 1: B accepts it
 *     as A's first record; A's fresh sequence 1 is then DROPPED_STALE, and
 *     its fresh sequence 2 FAILS B with NONCE_CHANGED.
 * (b) before any fresh record, the old RESOLVED record (sequence S), B idle:
 *     B accepts it; A's fresh sequences 1..S are DROPPED_STALE, and S + 1
 *     FAILS B with NONCE_CHANGED.
 * (c) as (b), but B finishes before A's sequence passes S: B resolves with
 *     the stale choice and nonce, and the stale resolvedDigest (another
 *     select's outcome) differs from B's, so B FAILS with DIGEST_MISMATCH.
 * (d) after fresh records, an old record with a lower sequence: dropped as
 *     stale; the select confirms normally.
 * (e) after fresh records, an old record re-sent with a higher sequence: B
 *     FAILS with NONCE_CHANGED.
 * In no case does any session confirm a wrong outcome: a failed B stops
 * composing, so A never sees a RESOLVED from B and fails by silence.
 */
static int CheckStaleFailure(const struct Net *net, uint32_t fault)
{
	CHECK(CheckNoWrongConfirm(net) == 0);
	CHECK(NativeMatchSelectSession_Status(&net->sessions[1]) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&net->sessions[1]) == fault);
	CHECK(NativeMatchSelectSession_Status(&net->sessions[0]) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&net->sessions[0]) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	return 0;
}

static int ScenarioStaleSelect(void)
{
	struct Net *net = &g_net;
	const uint8_t characters[2] = { 0, 1 };
	const uint8_t tracks[2] = { 3, 3 };
	const uint8_t laps[2] = { 3, 3 };
	const uint64_t freshNonces[2] = { UINT64_C(0x5555aaaa5555aaaa), UINT64_C(0x3c3c3c3cc3c3c3c3) };
	struct NativeMatchSelectOutcome oldOutcome;
	struct NativeMatchSelectOutcome freshOutcome;
	struct NativeMatchSelectMessageV1 oldResolved;
	uint8_t oldFirst[RECORD_BYTES];
	uint8_t oldLast[RECORD_BYTES];
	uint32_t oldSequence;

	/* The earlier select: capture A's first (PICKING, sequence 1) and last (RESOLVED) records. */
	CHECK(NetInit(net, 2, k_nonces, characters, tracks, laps, ITEM_TICKS, 10u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &oldOutcome) == 0);
	memcpy(oldFirst, net->firstComposed[0], sizeof(oldFirst));
	memcpy(oldLast, net->lastComposed[0], sizeof(oldLast));
	{
		struct NativeCodecReader reader;

		NativeCodecReader_Init(&reader, oldLast, sizeof(oldLast));
		CHECK(NativeMatchSelectMessageV1_Decode(&reader, &oldResolved, NULL));
		CHECK(oldResolved.phase == NATIVE_MATCH_SELECT_PHASE_RESOLVED && oldResolved.nonce == k_nonces[0]);
		oldSequence = oldResolved.sequence;
		CHECK(oldSequence > 8u && oldSequence < 80u);
	}

	/* (a) */
	CHECK(NetInit(net, 2, freshNonces, characters, tracks, laps, ITEM_TICKS, 11u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(NativeMatchSelectSession_Accept(&net->sessions[1], oldFirst, sizeof(oldFirst)) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckStaleFailure(net, NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED) == 0);
	CHECK(NativeMatchSelectSession_DroppedStale(&net->sessions[1]) == 1u);
	CHECK(net->failedTick[1] == 2u);
	printf("scenario 8a stale PICKING first: B FAILED/NONCE_CHANGED at %u, A FAILED/PEER_SILENT at %u\n", net->failedTick[1],
		net->failedTick[0]);

	/* (b) */
	CHECK(NetInit(net, 2, freshNonces, characters, tracks, laps, ITEM_TICKS, 12u) == 0);
	SetScript(net, 0, k_quickA, 3);
	CHECK(NativeMatchSelectSession_Accept(&net->sessions[1], oldLast, sizeof(oldLast)) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckStaleFailure(net, NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED) == 0);
	CHECK(NativeMatchSelectSession_DroppedStale(&net->sessions[1]) == oldSequence);
	CHECK(net->failedTick[1] == oldSequence + 1u);
	printf("scenario 8b stale RESOLVED first, B idle: %u fresh records dropped stale, B FAILED/NONCE_CHANGED at %u\n",
		oldSequence, net->failedTick[1]);

	/* (c) */
	CHECK(NetInit(net, 2, freshNonces, characters, tracks, laps, ITEM_TICKS, 13u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(NativeMatchSelectSession_Accept(&net->sessions[1], oldLast, sizeof(oldLast)) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckStaleFailure(net, NATIVE_MATCH_SELECT_SESSION_FAULT_DIGEST_MISMATCH) == 0);
	CHECK(net->failedTick[1] == 7u);
	printf("scenario 8c stale RESOLVED first, B quick: B FAILED/DIGEST_MISMATCH at %u, A FAILED/PEER_SILENT at %u\n",
		net->failedTick[1], net->failedTick[0]);

	/* (d) */
	CHECK(NetInit(net, 2, freshNonces, characters, tracks, laps, ITEM_TICKS, 14u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(RunTo(net, 5u) == 0);
	CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 0)->sequence >= 2u);
	CHECK(NativeMatchSelectSession_Accept(&net->sessions[1], oldFirst, sizeof(oldFirst)) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE);
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckAllConfirmedIdentical(net, &freshOutcome) == 0);
	CHECK(NativeMatchSelectSession_DroppedStale(&net->sessions[1]) == 1u);
	CHECK(freshOutcome.masterSeed != oldOutcome.masterSeed);
	printf("scenario 8d stale lower sequence after fresh: DROPPED_STALE, both CONFIRMED at %u/%u\n", net->confirmedTick[0],
		net->confirmedTick[1]);

	/* (e) */
	CHECK(NetInit(net, 2, freshNonces, characters, tracks, laps, ITEM_TICKS, 15u) == 0);
	SetScript(net, 0, k_quickA, 3);
	SetScript(net, 1, k_quickB, 3);
	CHECK(RunTo(net, 5u) == 0);
	{
		struct NativeMatchSelectMessageV1 resent = oldResolved;
		struct NativeCodecWriter writer;
		uint8_t bytes[RECORD_BYTES];

		resent.sequence = 100000u;
		NativeCodecWriter_Init(&writer, bytes, sizeof(bytes), NULL);
		CHECK(NativeMatchSelectMessageV1_Encode(&writer, &resent));
		CHECK(NativeMatchSelectSession_Accept(&net->sessions[1], bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_FAILED);
	}
	CHECK(RunUntilSettled(net) == 0);
	CHECK(CheckStaleFailure(net, NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED) == 0);
	printf("scenario 8e stale higher sequence after fresh: B FAILED/NONCE_CHANGED at %u, A FAILED/PEER_SILENT at %u\n",
		net->failedTick[1], net->failedTick[0]);
	return 0;
}

/*
 * 9. Four humans over six pairwise harnesses with loss, duplication, and
 * delay. Four votes over three lap options cannot split 1-1-1-1, so two runs
 * cover both shapes: (a) a 2-2 track tie with the most-split lap vote
 * possible (2-1-1) plus a simultaneous character lock; (b) a 1-1-1-1 track
 * split with a 2-2 lap tie.
 */
static const struct ScriptStep k_four0[] = { { 2, IN_CONFIRM }, { 4, IN_CONFIRM }, { 6, IN_CONFIRM } };
static const struct ScriptStep k_four1[] = { { 3, IN_CONFIRM }, { 5, IN_CONFIRM }, { 6, IN_NEXT }, { 7, IN_CONFIRM } };
static const struct ScriptStep k_four2[] = {
	{ 4, IN_CONFIRM }, { 5, IN_NEXT }, { 6, IN_CONFIRM }, { 7, IN_NEXT }, { 8, IN_NEXT }, { 9, IN_CONFIRM },
};
static const struct ScriptStep k_four3[] = { { 4, IN_CONFIRM }, { 5, IN_NEXT }, { 6, IN_CONFIRM }, { 7, IN_CONFIRM } };

static int ScenarioFourHumans(void)
{
	struct Net *net = &g_net;
	struct NativeMatchSelectOutcome outcome;

	/* (a) */
	{
		const uint8_t characters[4] = { 0, 1, 5, 5 };
		const uint8_t tracks[4] = { 3, 3, 3, 3 };
		const uint8_t laps[4] = { 3, 3, 3, 3 };

		CHECK(NetInit(net, 4, k_nonces, characters, tracks, laps, ITEM_TICKS, 16u) == 0);
		SetAllDirections(net, 20, 10, 3);
		SetScript(net, 0, k_four0, 3);
		SetScript(net, 1, k_four1, 4);
		SetScript(net, 2, k_four2, 6);
		SetScript(net, 3, k_four3, 4);
		CHECK(RunUntilSettled(net) == 0);
		CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
		CHECK(NativeMatchSelectSession_Human(&net->sessions[2], 2)->trackID == 6);
		CHECK(NativeMatchSelectSession_Human(&net->sessions[3], 3)->trackID == 6);
		CHECK(NativeMatchSelectSession_Human(&net->sessions[1], 1)->lapCount == 5);
		CHECK(NativeMatchSelectSession_Human(&net->sessions[2], 2)->lapCount == 7);
		CHECK(outcome.trackDrawn == 1 && (outcome.trackID == 3 || outcome.trackID == 6));
		CHECK(outcome.lapsDrawn == 0 && outcome.lapCount == 3);
		CHECK(outcome.humanCharacter[2] == 5 && outcome.humanCharacter[3] == 2 && outcome.characterReassignedMask == 0x8u);
		CHECK(net->dropped > 0 && net->duplicated > 0 && net->delayed > 0);
		printf("scenario 9a four humans, 2-2 track tie: all CONFIRMED by %u, drawn track %u, laps %u, human 3 reassigned to %u\n",
			LastConfirmedTick(net), outcome.trackID,
			outcome.lapCount, outcome.humanCharacter[3]);
	}

	/* (b) */
	{
		const uint8_t characters[4] = { 0, 1, 2, 3 };
		const uint8_t tracks[4] = { 3, 6, 4, 14 };
		const uint8_t laps[4] = { 3, 3, 5, 5 };

		CHECK(NetInit(net, 4, k_nonces, characters, tracks, laps, ITEM_TICKS, 17u) == 0);
		SetAllDirections(net, 20, 10, 3);
		SetScript(net, 0, k_quickA, 3);
		SetScript(net, 1, k_quickB, 3);
		SetScript(net, 2, k_quickA, 3);
		SetScript(net, 3, k_quickB, 3);
		CHECK(RunUntilSettled(net) == 0);
		CHECK(CheckAllConfirmedIdentical(net, &outcome) == 0);
		CHECK(outcome.trackDrawn == 1 &&
		      (outcome.trackID == 3 || outcome.trackID == 6 || outcome.trackID == 4 || outcome.trackID == 14));
		CHECK(outcome.lapsDrawn == 1 && (outcome.lapCount == 3 || outcome.lapCount == 5));
		CHECK(outcome.characterReassignedMask == 0);
		printf("scenario 9b four humans, 1-1-1-1 track split: all CONFIRMED by %u, drawn track %u, drawn laps %u\n",
			LastConfirmedTick(net), outcome.trackID, outcome.lapCount);
	}
	return 0;
}

int main(void)
{
	CHECK(PairIndex(0, 1) == 0 && PairIndex(0, 2) == 1 && PairIndex(0, 3) == 2);
	CHECK(PairIndex(1, 2) == 3 && PairIndex(1, 3) == 4 && PairIndex(2, 3) == 5 && PairIndex(3, 2) == 5);
	CHECK(ScenarioCleanAgreement() == 0);
	CHECK(ScenarioLossyDisagreement() == 0);
	CHECK(ScenarioIdleCabinet() == 0);
	CHECK(ScenarioSimultaneousCharacter() == 0);
	CHECK(ScenarioCorruption() == 0);
	CHECK(ScenarioSilence() == 0);
	CHECK(ScenarioLinger() == 0);
	CHECK(ScenarioStaleSelect() == 0);
	CHECK(ScenarioFourHumans() == 0);
	puts("native_match_select_session_fault_test: passed");
	return 0;
}
