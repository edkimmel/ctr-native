#include "platform/native_match_select_session.h"

#include "platform/native_match_config.h"
#include "platform/native_match_select_message.h"
#include "platform/native_match_select_rules.h"
#include "platform/native_sha256.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "%d: %s\n", __LINE__, #expression); return 1; } } while (0)

#define SENTINEL_BYTE 0xa5u
#define FIXTURE_SEED UINT64_C(0x4354524e41524331) /* "CTRNARC1" */
#define NONCE_A UINT64_C(0x1111111111111111)
#define NONCE_B UINT64_C(0x2222222222222222)
#define NONCE_C UINT64_C(0x3333333333333333)
#define RECORD_BYTES NATIVE_MATCH_SELECT_MESSAGE_V1_ENCODED_BYTES

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

static void BuildOneCabBase(struct NativeMatchConfigV1 *config)
{
	memset(config, 0, sizeof(*config));
	NativeMatchConfigV1_InitArcadeOneCab(config);
	config->trackID = 3;
	config->lapCount = 3;
	config->tickRateNumerator = 30;
	config->tickRateDenominator = 1;
	config->masterSeed = FIXTURE_SEED;
	FillCounting(config->buildIdentity, sizeof(config->buildIdentity), 0x40u);
	FillCounting(config->contentIdentity, sizeof(config->contentIdentity), 0x80u);
	FillCounting(config->botRulesDigest, sizeof(config->botRulesDigest), 0xc0u);
	for (uint32_t i = 0; i < NATIVE_MATCH_CONFIG_V1_SLOT_COUNT; i++)
	{
		config->slots[i].characterID = (uint8_t)i;
	}
}

static struct NativeMatchSelectTimings Timings(uint32_t character, uint32_t track, uint32_t laps, uint32_t silence)
{
	struct NativeMatchSelectTimings timings;

	timings.characterTicks = character;
	timings.trackTicks = track;
	timings.lapTicks = laps;
	timings.peerSilenceTicks = silence;
	return timings;
}

/* A PICKING message from sender to target's select, on item `item` with the matching lock mask. */
static void BaseMessage(struct NativeMatchSelectMessageV1 *message, const struct NativeMatchSelectSession *target,
	uint32_t sender, uint32_t sequence, uint64_t nonce, uint32_t item)
{
	memset(message, 0, sizeof(*message));
	message->senderHuman = (uint8_t)sender;
	message->humanCount = (uint8_t)target->humanCount;
	message->phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	message->currentItem = (uint8_t)item;
	message->lockMask = (uint8_t)((1u << item) - 1u);
	message->sequence = sequence;
	memcpy(message->baseDigest, target->baseDigest, sizeof(message->baseDigest));
	message->nonce = nonce;
	message->characterID = 0;
	message->trackID = 3;
	message->lapCount = 3;
}

static int Encode(const struct NativeMatchSelectMessageV1 *message, uint8_t bytes[RECORD_BYTES])
{
	struct NativeCodecWriter writer;

	NativeCodecWriter_Init(&writer, bytes, RECORD_BYTES, NULL);
	return NativeMatchSelectMessageV1_Encode(&writer, message) && (NativeCodecWriter_Size(&writer) == RECORD_BYTES);
}

static enum NativeMatchSelectAcceptResult Deliver(struct NativeMatchSelectSession *target,
	const struct NativeMatchSelectMessageV1 *message)
{
	uint8_t bytes[RECORD_BYTES];

	if (!Encode(message, bytes))
	{
		return (enum NativeMatchSelectAcceptResult)99;
	}
	return NativeMatchSelectSession_Accept(target, bytes, sizeof(bytes));
}

/* Compose on `from`, Accept on `to`. */
static enum NativeMatchSelectAcceptResult Transfer(struct NativeMatchSelectSession *from, struct NativeMatchSelectSession *to)
{
	uint8_t bytes[RECORD_BYTES];
	size_t size = 0;

	if (!NativeMatchSelectSession_Compose(from, bytes, sizeof(bytes), &size) || (size != RECORD_BYTES))
	{
		return (enum NativeMatchSelectAcceptResult)99;
	}
	return NativeMatchSelectSession_Accept(to, bytes, size);
}

static int Decode(const uint8_t *bytes, size_t size, struct NativeMatchSelectMessageV1 *message)
{
	struct NativeCodecReader reader;

	NativeCodecReader_Init(&reader, bytes, size);
	return NativeMatchSelectMessageV1_Decode(&reader, message, NULL);
}

static int ConfirmAll(struct NativeMatchSelectSession *session)
{
	return NativeMatchSelectSession_ApplyInput(session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) &&
	       NativeMatchSelectSession_ApplyInput(session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) &&
	       NativeMatchSelectSession_ApplyInput(session, NATIVE_MATCH_SELECT_INPUT_CONFIRM);
}

/* The resolvedDigest the session must compute for these choices (a direct Resolve). */
static int ExpectedDigest(const struct NativeMatchConfigV1 *base, uint32_t humanCount,
	const struct NativeMatchSelectChoice choices[], struct NativeMatchSelectOutcome *outcome,
	uint8_t resolved[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES])
{
	uint8_t baseDigest[NATIVE_SHA256_DIGEST_BYTES];
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];

	if (!NativeMatchConfigV1_Digest(base, baseDigest) || !NativeMatchSelect_Resolve(base, humanCount, choices, outcome) ||
	    !NativeMatchSelect_OutcomeDigest(baseDigest, outcome, digest))
	{
		return 0;
	}
	memcpy(resolved, digest, NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES);
	return 1;
}

static void SetChoice(struct NativeMatchSelectChoice *choice, uint8_t characterID, uint8_t trackID, uint8_t lapCount,
	uint64_t nonce)
{
	memset(choice, 0, sizeof(*choice));
	choice->characterID = characterID;
	choice->trackID = trackID;
	choice->lapCount = lapCount;
	choice->nonce = nonce;
}

static int TestDefaultsAndInit(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectTimings timings;
	struct NativeMatchSelectSession session;
	uint8_t digest[NATIVE_SHA256_DIGEST_BYTES];
	const struct NativeMatchSelectHumanState *local;
	const struct NativeMatchSelectHumanState *peer;

	CHECK(NATIVE_MATCH_SELECT_SESSION_DEFAULT_ITEM_TICKS == 600u);
	CHECK(NATIVE_MATCH_SELECT_SESSION_DEFAULT_PEER_SILENCE_TICKS == 90u);
	memset(&timings, 0, sizeof(timings));
	NativeMatchSelectSession_DefaultTimings(&timings);
	NativeMatchSelectSession_DefaultTimings(NULL);
	CHECK(timings.characterTicks == 600u && timings.trackTicks == 600u && timings.lapTicks == 600u);
	CHECK(timings.peerSilenceTicks == 90u);

	BuildTwoCabBase(&base);
	CHECK(NativeMatchConfigV1_Digest(&base, digest));
	memset(&session, SENTINEL_BYTE, sizeof(session));
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 1, NONCE_B, 4, 6, 5, NULL));
	CHECK(memcmp(&session.base, &base, sizeof(base)) == 0);
	CHECK(memcmp(session.baseDigest, digest, sizeof(digest)) == 0);
	CHECK(memcmp(&session.timings, &timings, sizeof(timings)) == 0);
	CHECK(NativeMatchSelectSession_Status(&session) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(NativeMatchSelectSession_Fault(&session) == NATIVE_MATCH_SELECT_SESSION_FAULT_NONE);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 600u);
	CHECK(NativeMatchSelectSession_Human(&session, 2) == NULL);
	local = NativeMatchSelectSession_Human(&session, 1);
	peer = NativeMatchSelectSession_Human(&session, 0);
	CHECK(local != NULL && peer != NULL);
	CHECK(local->seen == 1 && local->characterID == 4 && local->trackID == 6 && local->lapCount == 5);
	CHECK(local->lockMask == 0 && local->currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	CHECK(local->phase == NATIVE_MATCH_SELECT_PHASE_PICKING && local->sequence == 0 && local->nonce == NONCE_B);
	CHECK(peer->seen == 0 && peer->characterID == 0 && peer->lockMask == 0 && peer->sequence == 0 && peer->nonce == 0);
	CHECK(peer->silentTicks == 0);
	CHECK(NativeMatchSelectSession_Outcome(&session) == NULL);
	CHECK(NativeMatchSelectSession_ResolvedDigest(&session) == NULL);
	CHECK(NativeMatchSelectSession_DroppedMalformed(&session) == 0 && NativeMatchSelectSession_DroppedForeign(&session) == 0);
	CHECK(NativeMatchSelectSession_DroppedSelf(&session) == 0 && NativeMatchSelectSession_DroppedStale(&session) == 0);

	/* NULL-tolerant accessors. */
	CHECK(NativeMatchSelectSession_Status(NULL) == 0 && NativeMatchSelectSession_Fault(NULL) == 0);
	CHECK(NativeMatchSelectSession_CurrentItem(NULL) == 0 && NativeMatchSelectSession_TicksLeft(NULL) == 0);
	CHECK(NativeMatchSelectSession_Human(NULL, 0) == NULL && NativeMatchSelectSession_Outcome(NULL) == NULL);
	CHECK(NativeMatchSelectSession_ResolvedDigest(NULL) == NULL && NativeMatchSelectSession_CharacterLockedByPeer(NULL, 0) == 0);
	CHECK(NativeMatchSelectSession_DroppedMalformed(NULL) == 0 && NativeMatchSelectSession_DroppedForeign(NULL) == 0);
	CHECK(NativeMatchSelectSession_DroppedSelf(NULL) == 0 && NativeMatchSelectSession_DroppedStale(NULL) == 0);
	NativeMatchSelectSession_Tick(NULL);
	CHECK(NativeMatchSelectSession_ApplyInput(NULL, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 0);

	/* Custom timings are stored as given. */
	timings = Timings(5, 6, 7, 8);
	CHECK(NativeMatchSelectSession_Init(&session, &base, 1, 0, 0, 0, 3, 3, &timings));
	CHECK(memcmp(&session.timings, &timings, sizeof(timings)) == 0);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 5u);
	return 0;
}

static int TestInitValidation(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 invalid;
	struct NativeMatchSelectSession session;
	struct NativeMatchSelectSession sentinel;
	struct NativeMatchSelectTimings timings;

	BuildTwoCabBase(&base);
	invalid = base;
	invalid.configurationVersion = 99;
	CHECK(!NativeMatchConfigV1_Validate(&invalid));
	memset(&sentinel, SENTINEL_BYTE, sizeof(sentinel));

#define EXPECT_INIT_FAILS(call)                                              \
	do                                                                       \
	{                                                                        \
		memset(&session, SENTINEL_BYTE, sizeof(session));                    \
		CHECK(!(call));                                                      \
		CHECK(memcmp(&session, &sentinel, sizeof(session)) == 0);            \
	} while (0)

	CHECK(!NativeMatchSelectSession_Init(NULL, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, NULL, 2, 0, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &invalid, 2, 0, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 0, 0, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 5, 0, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 2, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 1, 1, NONCE_A, 0, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 8, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 15, 3, 3, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 13, 3, NULL)); /* Oxide Station */
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 17, 3, NULL)); /* Turbo Track */
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 4, NULL));
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 0, NULL));
	timings = Timings(0, 1, 1, 1);
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
	timings = Timings(1, 0, 1, 1);
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
	timings = Timings(1, 1, 0, 1);
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
	timings = Timings(1, 1, 1, 0);
	EXPECT_INIT_FAILS(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
#undef EXPECT_INIT_FAILS

	/* Every humanCount 1..4 and every localHuman below it initializes. */
	for (uint32_t count = 1; count <= NATIVE_MATCH_SELECT_MAX_HUMANS; count++)
	{
		for (uint32_t local = 0; local < count; local++)
		{
			CHECK(NativeMatchSelectSession_Init(&session, &base, count, local, NONCE_A, 7, 16, 7, NULL));
			CHECK(session.humanCount == count && session.localHuman == local);
		}
	}

	/* An uninitialized (zeroed) session rejects everything. */
	memset(&session, 0, sizeof(session));
	{
		uint8_t bytes[RECORD_BYTES];
		size_t size = 0;

		CHECK(NativeMatchSelectSession_Accept(&session, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_REJECTED_LOCAL_STATE);
		CHECK(NativeMatchSelectSession_Accept(NULL, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_REJECTED_LOCAL_STATE);
		CHECK(!NativeMatchSelectSession_Compose(&session, bytes, sizeof(bytes), &size));
		CHECK(!NativeMatchSelectSession_Compose(NULL, bytes, sizeof(bytes), &size));
		CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 0);
		NativeMatchSelectSession_Tick(&session);
		CHECK(session.itemTicks == 0);
	}
	return 0;
}

static int TestCursorWrap(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession session;
	const struct NativeMatchSelectHumanState *local;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	local = NativeMatchSelectSession_Human(&session, 0);

	/* Characters 0..7: PREV from the first wraps to the last, NEXT from the last to the first. */
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	CHECK(local->characterID == 7);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->characterID == 0);
	for (uint32_t i = 1; i <= NATIVE_MATCH_SELECT_CHARACTER_COUNT; i++)
	{
		CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
		CHECK(local->characterID == NativeMatchSelect_CharacterAt(i % NATIVE_MATCH_SELECT_CHARACTER_COUNT));
	}
	/* Only the current item's cursor moves. */
	CHECK(local->trackID == 3 && local->lapCount == 3 && local->lockMask == 0);

	/* Tracks in retail menu order: 3 is first, 16 last. */
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_TRACK);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	CHECK(local->trackID == 16);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->trackID == 3);
	for (uint32_t i = 1; i <= NATIVE_MATCH_SELECT_TRACK_COUNT; i++)
	{
		CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
		CHECK(local->trackID == NativeMatchSelect_TrackAt(i % NATIVE_MATCH_SELECT_TRACK_COUNT));
	}
	for (uint32_t i = 1; i <= NATIVE_MATCH_SELECT_TRACK_COUNT; i++)
	{
		CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
		CHECK(local->trackID == NativeMatchSelect_TrackAt((NATIVE_MATCH_SELECT_TRACK_COUNT - i) % NATIVE_MATCH_SELECT_TRACK_COUNT));
	}
	CHECK(local->characterID == 0 && local->lapCount == 3);

	/* Laps 3, 5, 7. */
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->trackID == 6);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_LAPS);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	CHECK(local->lapCount == 7);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->lapCount == 3);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->lapCount == 5);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->lapCount == 7);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(local->lapCount == 3);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	CHECK(local->lapCount == 7);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	CHECK(local->lapCount == 5);
	CHECK(local->trackID == 6 && local->characterID == 0 && local->lockMask == 0x3u);
	return 0;
}

static int TestConfirmAndNoOps(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession session;
	struct NativeMatchSelectSession before;
	struct NativeMatchSelectTimings timings = Timings(10, 11, 12, 100);
	const struct NativeMatchSelectHumanState *local;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 2, 6, 5, &timings));
	local = NativeMatchSelectSession_Human(&session, 0);

	/* BACK (SEL-7), NONE, and unknown inputs change nothing. */
	memcpy(&before, &session, sizeof(before));
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_BACK) == 0);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NONE) == 0);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, (enum NativeMatchSelectInput)99) == 0);
	CHECK(memcmp(&session, &before, sizeof(session)) == 0);

	/* CONFIRM locks the cursor, advances, and restarts the countdown. */
	NativeMatchSelectSession_Tick(&session);
	NativeMatchSelectSession_Tick(&session);
	NativeMatchSelectSession_Tick(&session);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 7u);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(local->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER && local->characterID == 2);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_TRACK);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 11u);
	CHECK(NativeMatchSelectSession_Status(&session) == NATIVE_MATCH_SELECT_STATUS_PICKING);

	/* BACK does not unlock. */
	memcpy(&before, &session, sizeof(before));
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_BACK) == 0);
	CHECK(memcmp(&session, &before, sizeof(session)) == 0);

	NativeMatchSelectSession_Tick(&session);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(local->lockMask == 0x3u && local->trackID == 6);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 12u);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(local->lockMask == 0x7u && local->lapCount == 5);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_DONE);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 0u);
	CHECK(NativeMatchSelectSession_Status(&session) == NATIVE_MATCH_SELECT_STATUS_WAITING);

	/* Once DONE every input is ignored. */
	memcpy(&before, &session, sizeof(before));
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 0);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 0);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 0);
	CHECK(memcmp(&session, &before, sizeof(session)) == 0);
	return 0;
}

static int TestRefusedConfirmOnPeerCharacter(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectSession b;
	const struct NativeMatchSelectHumanState *bLocal;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 2, 3, 3, NULL));
	CHECK(NativeMatchSelectSession_Init(&b, &base, 2, 1, NONCE_B, 2, 3, 3, NULL));
	bLocal = NativeMatchSelectSession_Human(&b, 1);

	/* A's cursor on 2 (not locked) does not block B. */
	CHECK(Transfer(&a, &b) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_CharacterLockedByPeer(&b, 2) == 0);

	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(Transfer(&a, &b) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_CharacterLockedByPeer(&b, 2) == 1);
	CHECK(NativeMatchSelectSession_CharacterLockedByPeer(&b, 3) == 0);
	/* The local human's own lock never counts as a peer's. */
	CHECK(NativeMatchSelectSession_CharacterLockedByPeer(&a, 2) == 0);

	/* The MM_Characters_boolIsInvalid rule: refused, nothing changes. */
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 0);
	CHECK(bLocal->lockMask == 0 && bLocal->currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER && bLocal->characterID == 2);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(bLocal->characterID == 3 && bLocal->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER);

	/* The rule only applies to characters: a track or lap vote may equal the peer's. */
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(Transfer(&a, &b) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(bLocal->trackID == 3 && bLocal->lockMask == 0x3u);
	return 0;
}

static int TestExpiryAutoLock(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession session;
	struct NativeMatchSelectTimings timings = Timings(5, 6, 7, 100);
	struct NativeMatchSelectMessageV1 message;
	const struct NativeMatchSelectHumanState *local;

	BuildTwoCabBase(&base);

	/* Each item locks its cursor when its countdown runs out. */
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 0, NONCE_A, 4, 3, 3, &timings));
	local = NativeMatchSelectSession_Human(&session, 0);
	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	for (uint32_t i = 0; i < 4u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 1u);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	NativeMatchSelectSession_Tick(&session);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_TRACK);
	CHECK(local->characterID == 5 && local->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 6u);

	CHECK(NativeMatchSelectSession_ApplyInput(&session, NATIVE_MATCH_SELECT_INPUT_PREV) == 1);
	for (uint32_t i = 0; i < 5u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 1u);
	NativeMatchSelectSession_Tick(&session);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_LAPS);
	CHECK(local->trackID == 16 && local->lockMask == 0x3u);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 7u);

	for (uint32_t i = 0; i < 6u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 1u);
	NativeMatchSelectSession_Tick(&session);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_DONE);
	CHECK(local->lapCount == 3 && local->lockMask == 0x7u);
	CHECK(NativeMatchSelectSession_TicksLeft(&session) == 0u);
	CHECK(NativeMatchSelectSession_Status(&session) == NATIVE_MATCH_SELECT_STATUS_WAITING);
	NativeMatchSelectSession_Tick(&session);
	CHECK(session.itemTicks == 0 && NativeMatchSelectSession_TicksLeft(&session) == 0u);

	/* SEL-6: the cursor's character is a peer's, so the next free one after it. */
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 1, NONCE_B, 3, 3, 3, &timings));
	local = NativeMatchSelectSession_Human(&session, 1);
	BaseMessage(&message, &session, 0, 1, NONCE_A, NATIVE_MATCH_SELECT_ITEM_TRACK);
	message.characterID = 3;
	CHECK(Deliver(&session, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	for (uint32_t i = 0; i < 5u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(local->characterID == 4 && local->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER);

	/* A peer's unlocked cursor on the character does not move the pick. */
	CHECK(NativeMatchSelectSession_Init(&session, &base, 2, 1, NONCE_B, 3, 3, 3, &timings));
	local = NativeMatchSelectSession_Human(&session, 1);
	BaseMessage(&message, &session, 0, 1, NONCE_A, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	message.characterID = 3;
	CHECK(Deliver(&session, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	for (uint32_t i = 0; i < 5u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(local->characterID == 3 && local->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER);

	/* Wrap and multi-skip: peers hold 7 and 0, the cursor is on 7 -> 1. */
	CHECK(NativeMatchSelectSession_Init(&session, &base, 3, 2, NONCE_C, 7, 3, 3, &timings));
	local = NativeMatchSelectSession_Human(&session, 2);
	BaseMessage(&message, &session, 0, 1, NONCE_A, NATIVE_MATCH_SELECT_ITEM_TRACK);
	message.characterID = 7;
	CHECK(Deliver(&session, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	BaseMessage(&message, &session, 1, 1, NONCE_B, NATIVE_MATCH_SELECT_ITEM_LAPS);
	message.characterID = 0;
	CHECK(Deliver(&session, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	for (uint32_t i = 0; i < 4u; i++)
	{
		NativeMatchSelectSession_Tick(&session);
	}
	CHECK(local->lockMask == 0 && local->characterID == 7);
	NativeMatchSelectSession_Tick(&session);
	CHECK(local->characterID == 1 && local->lockMask == NATIVE_MATCH_SELECT_LOCK_CHARACTER);
	CHECK(NativeMatchSelectSession_CurrentItem(&session) == NATIVE_MATCH_SELECT_ITEM_TRACK);
	return 0;
}

static int TestPeerSilence(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectSession b;
	struct NativeMatchSelectTimings timings = Timings(100, 100, 100, 10);
	uint8_t bytes[RECORD_BYTES];
	uint8_t untouched[RECORD_BYTES];
	size_t size = 0;
	uint32_t sequence;

	BuildTwoCabBase(&base);

	/* A peer that never sent counts from Init: FAILED exactly at the limit, not one tick before. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
	for (uint32_t i = 0; i < 9u; i++)
	{
		NativeMatchSelectSession_Tick(&a);
	}
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->silentTicks == 9u);
	NativeMatchSelectSession_Tick(&a);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);

	/* An accepted message restarts the count. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
	CHECK(NativeMatchSelectSession_Init(&b, &base, 2, 1, NONCE_B, 1, 3, 3, &timings));
	for (uint32_t i = 0; i < 9u; i++)
	{
		NativeMatchSelectSession_Tick(&a);
	}
	CHECK(Transfer(&b, &a) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->silentTicks == 0u);
	for (uint32_t i = 0; i < 9u; i++)
	{
		NativeMatchSelectSession_Tick(&a);
	}
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	/* A dropped (stale) record does not restart it. */
	{
		struct NativeMatchSelectMessageV1 message;

		BaseMessage(&message, &a, 1, 1, NONCE_B, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
		message.characterID = 1;
		CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE);
	}
	NativeMatchSelectSession_Tick(&a);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);

	/* FAILED latches: Tick, input, and Accept change nothing; Compose is refused. */
	sequence = NativeMatchSelectSession_Human(&a, 0)->sequence;
	memset(bytes, SENTINEL_BYTE, sizeof(bytes));
	memset(untouched, SENTINEL_BYTE, sizeof(untouched));
	size = 77;
	CHECK(!NativeMatchSelectSession_Compose(&a, bytes, sizeof(bytes), &size));
	CHECK(size == 77 && memcmp(bytes, untouched, sizeof(bytes)) == 0);
	CHECK(NativeMatchSelectSession_Human(&a, 0)->sequence == sequence);
	NativeMatchSelectSession_Tick(&a);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 0);
	CHECK(Transfer(&b, &a) == NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
	return 0;
}

static int TestAcceptDrops(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchConfigV1 otherBase;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectSession b;
	struct NativeMatchSelectSession foreign;
	struct NativeMatchSelectSession before;
	struct NativeMatchSelectMessageV1 message;
	uint8_t bytes[RECORD_BYTES];
	uint8_t older[RECORD_BYTES];
	size_t size = 0;

	BuildTwoCabBase(&base);
	otherBase = base;
	otherBase.masterSeed = FIXTURE_SEED + 1u;
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	CHECK(NativeMatchSelectSession_Init(&b, &base, 2, 1, NONCE_B, 1, 3, 3, NULL));
	CHECK(NativeMatchSelectSession_Init(&foreign, &otherBase, 2, 1, NONCE_B, 1, 3, 3, NULL));

	/* Malformed: NULL, short, long, a flipped byte. Counted; nothing else changes. */
	CHECK(NativeMatchSelectSession_Compose(&b, bytes, sizeof(bytes), &size));
	memcpy(&before, &a, sizeof(before));
	CHECK(NativeMatchSelectSession_Accept(&a, NULL, RECORD_BYTES) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED);
	CHECK(NativeMatchSelectSession_Accept(&a, bytes, RECORD_BYTES - 1u) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED);
	{
		uint8_t longer[RECORD_BYTES + 1u];

		memcpy(longer, bytes, sizeof(bytes));
		longer[RECORD_BYTES] = 0;
		CHECK(NativeMatchSelectSession_Accept(&a, longer, sizeof(longer)) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED);
	}
	bytes[33] ^= 0x01u;
	CHECK(NativeMatchSelectSession_Accept(&a, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_MALFORMED);
	bytes[33] ^= 0x01u;
	CHECK(NativeMatchSelectSession_DroppedMalformed(&a) == 4u);
	before.droppedMalformed = 4u;
	CHECK(memcmp(&a, &before, sizeof(a)) == 0);
	/* The same record, intact, is accepted. */
	CHECK(NativeMatchSelectSession_Accept(&a, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->seen == 1);

	/* Foreign: another select's base digest. Checked before humanCount. */
	CHECK(Transfer(&foreign, &a) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_FOREIGN);
	BaseMessage(&message, &foreign, 1, 50, NONCE_B, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	message.humanCount = 3;
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_FOREIGN);
	CHECK(NativeMatchSelectSession_DroppedForeign(&a) == 2u);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == 1u);

	/* Self: our own senderHuman. */
	CHECK(Transfer(&a, &a) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_SELF);
	BaseMessage(&message, &a, 0, 1000, NONCE_C, NATIVE_MATCH_SELECT_ITEM_DONE);
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_SELF);
	CHECK(NativeMatchSelectSession_DroppedSelf(&a) == 2u);
	CHECK(NativeMatchSelectSession_Human(&a, 0)->nonce == NONCE_A);

	/* Stale: a duplicate, and an older record after a newer one. Checked before the nonce. */
	CHECK(NativeMatchSelectSession_Compose(&b, older, sizeof(older), &size));  /* sequence 2 */
	CHECK(NativeMatchSelectSession_Compose(&b, bytes, sizeof(bytes), &size));  /* sequence 3 */
	CHECK(NativeMatchSelectSession_Accept(&a, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Accept(&a, bytes, sizeof(bytes)) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE);
	CHECK(NativeMatchSelectSession_Accept(&a, older, sizeof(older)) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE);
	BaseMessage(&message, &a, 1, 3, NONCE_C, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_DROPPED_STALE);
	CHECK(NativeMatchSelectSession_DroppedStale(&a) == 3u);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == 3u);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_PICKING);

	/* A sender's first record may carry any sequence and locks. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	BaseMessage(&message, &a, 1, 12345, NONCE_B, NATIVE_MATCH_SELECT_ITEM_LAPS);
	message.characterID = 6;
	message.trackID = 9;
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == 12345u && NativeMatchSelectSession_Human(&a, 1)->lockMask == 0x3u);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->characterID == 6 && NativeMatchSelectSession_Human(&a, 1)->trackID == 9);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->currentItem == NATIVE_MATCH_SELECT_ITEM_LAPS);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->nonce == NONCE_B);
	return 0;
}

/* One Accept that must latch FAILED with `fault`, after `setup` (a record the session accepts). */
static int ExpectAcceptFails(const struct NativeMatchSelectMessageV1 *setup, const struct NativeMatchSelectMessageV1 *bad,
	uint32_t fault)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectMessageV1 later;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	if (setup != NULL)
	{
		CHECK(Deliver(&a, setup) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	}
	CHECK(Deliver(&a, bad) == NATIVE_MATCH_SELECT_ACCEPT_FAILED);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == fault);

	/* Latch-once: a further (even valid) record is ignored and the fault stays. */
	later = *bad;
	later.sequence += 100u;
	later.humanCount = 3;
	CHECK(Deliver(&a, &later) == NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL);
	CHECK(NativeMatchSelectSession_Fault(&a) == fault);
	return 0;
}

static int TestAcceptFailures(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession target;
	struct NativeMatchSelectMessageV1 setup;
	struct NativeMatchSelectMessageV1 bad;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&target, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));

	/* humanCount mismatch on the same base; checked before the self check. */
	BaseMessage(&bad, &target, 1, 1, NONCE_B, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	bad.humanCount = 3;
	CHECK(ExpectAcceptFails(NULL, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_HUMAN_COUNT_MISMATCH) == 0);
	BaseMessage(&bad, &target, 0, 1, NONCE_A, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	bad.humanCount = 1;
	CHECK(ExpectAcceptFails(NULL, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_HUMAN_COUNT_MISMATCH) == 0);

	/* Nonce change. */
	BaseMessage(&setup, &target, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	bad = setup;
	bad.sequence = 6;
	bad.nonce = NONCE_C;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_NONCE_CHANGED) == 0);

	/* A locked character changes value. */
	BaseMessage(&setup, &target, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_TRACK);
	setup.characterID = 4;
	bad = setup;
	bad.sequence = 6;
	bad.characterID = 5;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);
	/* ... or unlocks. */
	bad = setup;
	bad.sequence = 6;
	bad.currentItem = NATIVE_MATCH_SELECT_ITEM_CHARACTER;
	bad.lockMask = 0;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);

	/* A locked track vote changes value, or unlocks. */
	BaseMessage(&setup, &target, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_LAPS);
	setup.trackID = 9;
	bad = setup;
	bad.sequence = 6;
	bad.trackID = 3;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);
	bad = setup;
	bad.sequence = 6;
	bad.currentItem = NATIVE_MATCH_SELECT_ITEM_TRACK;
	bad.lockMask = 0x1u;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);

	/* A locked lap vote changes value. */
	BaseMessage(&setup, &target, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_DONE);
	setup.lapCount = 7;
	bad = setup;
	bad.sequence = 6;
	bad.lapCount = 5;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);

	/* An unlocked cursor may move freely. */
	{
		struct NativeMatchSelectSession a;

		CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
		BaseMessage(&setup, &a, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_TRACK);
		setup.characterID = 4;
		setup.trackID = 9;
		CHECK(Deliver(&a, &setup) == NATIVE_MATCH_SELECT_ACCEPT_OK);
		setup.sequence = 6;
		setup.trackID = 12;
		setup.lapCount = 7;
		CHECK(Deliver(&a, &setup) == NATIVE_MATCH_SELECT_ACCEPT_OK);
		CHECK(NativeMatchSelectSession_Human(&a, 1)->trackID == 12 && NativeMatchSelectSession_Human(&a, 1)->lapCount == 7);
	}

	/* A sender seen RESOLVED must stay RESOLVED with the same digest. */
	BaseMessage(&setup, &target, 1, 5, NONCE_B, NATIVE_MATCH_SELECT_ITEM_DONE);
	setup.phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	memset(setup.resolvedDigest, 0x5au, sizeof(setup.resolvedDigest));
	bad = setup;
	bad.sequence = 6;
	bad.phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
	memset(bad.resolvedDigest, 0, sizeof(bad.resolvedDigest));
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);
	bad = setup;
	bad.sequence = 6;
	bad.resolvedDigest[7] ^= 0x01u;
	CHECK(ExpectAcceptFails(&setup, &bad, NATIVE_MATCH_SELECT_SESSION_FAULT_LOCK_CHANGED) == 0);

	/* Resolution failure: two humans on a one-cab base (seven bots) cannot resolve. */
	{
		struct NativeMatchConfigV1 oneCab;
		struct NativeMatchSelectSession a;

		BuildOneCabBase(&oneCab);
		CHECK(NativeMatchSelectSession_Init(&a, &oneCab, 2, 0, NONCE_A, 0, 3, 3, NULL));
		CHECK(ConfirmAll(&a));
		CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_WAITING);
		BaseMessage(&bad, &a, 1, 1, NONCE_B, NATIVE_MATCH_SELECT_ITEM_DONE);
		bad.characterID = 1;
		CHECK(Deliver(&a, &bad) == NATIVE_MATCH_SELECT_ACCEPT_FAILED);
		CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
		CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_RESOLVE_FAILED);
		CHECK(NativeMatchSelectSession_Outcome(&a) == NULL);
	}
	return 0;
}

static int TestSingleHumanConfirms(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession solo;
	struct NativeMatchSelectChoice choice;
	struct NativeMatchSelectOutcome expected;
	struct NativeMatchSelectMessageV1 message;
	uint8_t resolved[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];
	uint8_t bytes[RECORD_BYTES];
	size_t size = 0;

	BuildOneCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&solo, &base, 1, 0, NONCE_A, 5, 3, 3, NULL));
	CHECK(NativeMatchSelectSession_ApplyInput(&solo, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&solo, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&solo, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_Status(&solo) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(NativeMatchSelectSession_Outcome(&solo) == NULL);
	CHECK(NativeMatchSelectSession_ApplyInput(&solo, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	/* Resolved and confirmed at once: every (zero) peer agrees. */
	CHECK(NativeMatchSelectSession_Status(&solo) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);

	SetChoice(&choice, 5, 6, 3, NONCE_A);
	memset(&expected, 0, sizeof(expected)); /* the outcome has trailing padding; whole-struct memcmp below */
	CHECK(ExpectedDigest(&base, 1, &choice, &expected, resolved));
	CHECK(NativeMatchSelectSession_Outcome(&solo) != NULL);
	CHECK(memcmp(NativeMatchSelectSession_Outcome(&solo), &expected, sizeof(expected)) == 0);
	CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(&solo), resolved, sizeof(resolved)) == 0);

	/* CONFIRMED is terminal but still composes (the linger), as RESOLVED. */
	CHECK(NativeMatchSelectSession_Compose(&solo, bytes, sizeof(bytes), &size));
	CHECK(Decode(bytes, size, &message));
	CHECK(message.phase == NATIVE_MATCH_SELECT_PHASE_RESOLVED && message.lockMask == 0x7u && message.sequence == 1u);
	CHECK(memcmp(message.resolvedDigest, resolved, sizeof(resolved)) == 0);
	NativeMatchSelectSession_Tick(&solo);
	CHECK(NativeMatchSelectSession_ApplyInput(&solo, NATIVE_MATCH_SELECT_INPUT_NEXT) == 0);
	CHECK(NativeMatchSelectSession_Accept(&solo, bytes, size) == NATIVE_MATCH_SELECT_ACCEPT_IGNORED_TERMINAL);
	CHECK(NativeMatchSelectSession_DroppedSelf(&solo) == 0);
	CHECK(NativeMatchSelectSession_Status(&solo) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);

	/* Auto-pick alone also confirms, at the last expiry. */
	{
		struct NativeMatchSelectTimings timings = Timings(2, 2, 2, 1);

		CHECK(NativeMatchSelectSession_Init(&solo, &base, 1, 0, NONCE_A, 0, 3, 3, &timings));
		for (uint32_t i = 0; i < 5u; i++)
		{
			NativeMatchSelectSession_Tick(&solo);
		}
		CHECK(NativeMatchSelectSession_Status(&solo) == NATIVE_MATCH_SELECT_STATUS_PICKING);
		NativeMatchSelectSession_Tick(&solo);
		CHECK(NativeMatchSelectSession_Status(&solo) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	}
	return 0;
}

static int TestTwoHumanAgreement(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectSession b;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome expected;
	uint8_t resolved[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	CHECK(NativeMatchSelectSession_Init(&b, &base, 2, 1, NONCE_B, 1, 3, 3, NULL));

	/* A finishes first: WAITING, and no outcome until B has locked everything. */
	CHECK(ConfirmAll(&a));
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_WAITING);
	CHECK(Transfer(&b, &a) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_WAITING);
	CHECK(NativeMatchSelectSession_Outcome(&a) == NULL && NativeMatchSelectSession_ResolvedDigest(&a) == NULL);
	CHECK(Transfer(&a, &b) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&b) == NATIVE_MATCH_SELECT_STATUS_PICKING);

	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_NEXT) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&b, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	/* B knows all of A's locks: resolved now, awaiting A's RESOLVED. */
	CHECK(NativeMatchSelectSession_Status(&b) == NATIVE_MATCH_SELECT_STATUS_RESOLVED);
	CHECK(NativeMatchSelectSession_Outcome(&b) != NULL && NativeMatchSelectSession_ResolvedDigest(&b) != NULL);

	SetChoice(&choices[0], 0, 3, 3, NONCE_A);
	SetChoice(&choices[1], 1, 6, 3, NONCE_B);
	memset(&expected, 0, sizeof(expected)); /* the outcome has trailing padding; whole-struct memcmp below */
	CHECK(ExpectedDigest(&base, 2, choices, &expected, resolved));
	CHECK(memcmp(NativeMatchSelectSession_Outcome(&b), &expected, sizeof(expected)) == 0);
	CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(&b), resolved, sizeof(resolved)) == 0);
	CHECK(expected.trackDrawn == 1 && expected.lapsDrawn == 0 && expected.lapCount == 3);

	/* A gets B's RESOLVED before resolving itself: it resolves, compares, and confirms at once. */
	CHECK(Transfer(&b, &a) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(memcmp(NativeMatchSelectSession_Outcome(&a), &expected, sizeof(expected)) == 0);
	CHECK(Transfer(&a, &b) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&b) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(&a), NativeMatchSelectSession_ResolvedDigest(&b), 8u) == 0);
	return 0;
}

static int TestPeerResolvedComparedOnResolve(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectMessageV1 message;
	struct NativeMatchSelectChoice choices[2];
	struct NativeMatchSelectOutcome expected;
	uint8_t resolved[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES];

	BuildTwoCabBase(&base);
	SetChoice(&choices[0], 0, 3, 3, NONCE_A);
	SetChoice(&choices[1], 4, 9, 7, NONCE_B);
	memset(&expected, 0, sizeof(expected)); /* the outcome has trailing padding; whole-struct memcmp below */
	CHECK(ExpectedDigest(&base, 2, choices, &expected, resolved));

	/* The peer's RESOLVED arrives while we still pick: stored, compared when we resolve. Equal: CONFIRMED. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	BaseMessage(&message, &a, 1, 9, NONCE_B, NATIVE_MATCH_SELECT_ITEM_DONE);
	message.phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	message.characterID = 4;
	message.trackID = 9;
	message.lapCount = 7;
	memcpy(message.resolvedDigest, resolved, sizeof(resolved));
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_PICKING);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_CONFIRMED);
	CHECK(memcmp(NativeMatchSelectSession_Outcome(&a), &expected, sizeof(expected)) == 0);

	/* Different: DIGEST_MISMATCH, and no outcome is exposed. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	message.resolvedDigest[0] ^= 0x80u;
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_DIGEST_MISMATCH);
	CHECK(a.resolved == 1);
	CHECK(NativeMatchSelectSession_Outcome(&a) == NULL && NativeMatchSelectSession_ResolvedDigest(&a) == NULL);

	/* The same mismatch reached through Accept, after we resolved: Accept reports FAILED. */
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, NULL));
	CHECK(ConfirmAll(&a));
	BaseMessage(&message, &a, 1, 1, NONCE_B, NATIVE_MATCH_SELECT_ITEM_DONE);
	message.characterID = 4;
	message.trackID = 9;
	message.lapCount = 7;
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
	CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_RESOLVED);
	CHECK(memcmp(NativeMatchSelectSession_ResolvedDigest(&a), resolved, sizeof(resolved)) == 0);
	message.sequence = 2;
	message.phase = NATIVE_MATCH_SELECT_PHASE_RESOLVED;
	memcpy(message.resolvedDigest, resolved, sizeof(resolved));
	message.resolvedDigest[3] ^= 0x10u;
	CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_FAILED);
	CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_DIGEST_MISMATCH);

	/* RESOLVED with silence afterwards: resolved, then FAILED, so no outcome. */
	{
		struct NativeMatchSelectTimings timings = Timings(10, 10, 10, 3);

		CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 0, NONCE_A, 0, 3, 3, &timings));
		CHECK(ConfirmAll(&a));
		message.sequence = 1;
		message.phase = NATIVE_MATCH_SELECT_PHASE_PICKING;
		memset(message.resolvedDigest, 0, sizeof(message.resolvedDigest));
		CHECK(Deliver(&a, &message) == NATIVE_MATCH_SELECT_ACCEPT_OK);
		CHECK(NativeMatchSelectSession_Outcome(&a) != NULL);
		NativeMatchSelectSession_Tick(&a);
		NativeMatchSelectSession_Tick(&a);
		CHECK(NativeMatchSelectSession_Status(&a) == NATIVE_MATCH_SELECT_STATUS_RESOLVED);
		NativeMatchSelectSession_Tick(&a);
		CHECK(NativeMatchSelectSession_Fault(&a) == NATIVE_MATCH_SELECT_SESSION_FAULT_PEER_SILENT);
		CHECK(NativeMatchSelectSession_Outcome(&a) == NULL && NativeMatchSelectSession_ResolvedDigest(&a) == NULL);
	}
	return 0;
}

static int TestCompose(void)
{
	struct NativeMatchConfigV1 base;
	struct NativeMatchSelectSession a;
	struct NativeMatchSelectMessageV1 message;
	uint8_t bytes[RECORD_BYTES + 4u];
	uint8_t untouched[RECORD_BYTES + 4u];
	size_t size = 0;
	uint32_t last = 0;

	BuildTwoCabBase(&base);
	CHECK(NativeMatchSelectSession_Init(&a, &base, 2, 1, NONCE_B, 6, 12, 5, NULL));

	/* The composed sequence strictly increases, from 1. */
	for (uint32_t i = 0; i < 5u; i++)
	{
		CHECK(NativeMatchSelectSession_Compose(&a, bytes, sizeof(bytes), &size));
		CHECK(size == RECORD_BYTES);
		CHECK(Decode(bytes, size, &message));
		CHECK(message.sequence == last + 1u);
		CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == message.sequence);
		last = message.sequence;
	}
	CHECK(message.senderHuman == 1 && message.humanCount == 2 && message.phase == NATIVE_MATCH_SELECT_PHASE_PICKING);
	CHECK(message.lockMask == 0 && message.currentItem == NATIVE_MATCH_SELECT_ITEM_CHARACTER);
	CHECK(memcmp(message.baseDigest, a.baseDigest, sizeof(message.baseDigest)) == 0);
	CHECK(message.nonce == NONCE_B && message.characterID == 6 && message.trackID == 12 && message.lapCount == 5);
	{
		const uint8_t zero[NATIVE_MATCH_SELECT_RESOLVED_DIGEST_BYTES] = { 0 };

		CHECK(memcmp(message.resolvedDigest, zero, sizeof(zero)) == 0);
	}

	/* Locks and item replicate. */
	CHECK(NativeMatchSelectSession_ApplyInput(&a, NATIVE_MATCH_SELECT_INPUT_CONFIRM) == 1);
	CHECK(NativeMatchSelectSession_Compose(&a, bytes, sizeof(bytes), &size));
	CHECK(Decode(bytes, size, &message));
	CHECK(message.lockMask == 0x1u && message.currentItem == NATIVE_MATCH_SELECT_ITEM_TRACK && message.sequence == 6u);

	/* Refusals leave the bytes, *sizeOut, and the sequence untouched. */
	memset(bytes, SENTINEL_BYTE, sizeof(bytes));
	memcpy(untouched, bytes, sizeof(bytes));
	size = 77;
	CHECK(!NativeMatchSelectSession_Compose(&a, bytes, RECORD_BYTES - 1u, &size));
	CHECK(!NativeMatchSelectSession_Compose(&a, NULL, sizeof(bytes), &size));
	CHECK(!NativeMatchSelectSession_Compose(&a, bytes, sizeof(bytes), NULL));
	CHECK(size == 77 && memcmp(bytes, untouched, sizeof(bytes)) == 0);
	CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == 6u);

	/* The sequence cannot wrap to 0. */
	a.humans[1].sequence = UINT32_MAX;
	CHECK(!NativeMatchSelectSession_Compose(&a, bytes, sizeof(bytes), &size));
	CHECK(NativeMatchSelectSession_Human(&a, 1)->sequence == UINT32_MAX);
	return 0;
}

int main(void)
{
	CHECK(TestDefaultsAndInit() == 0);
	CHECK(TestInitValidation() == 0);
	CHECK(TestCursorWrap() == 0);
	CHECK(TestConfirmAndNoOps() == 0);
	CHECK(TestRefusedConfirmOnPeerCharacter() == 0);
	CHECK(TestExpiryAutoLock() == 0);
	CHECK(TestPeerSilence() == 0);
	CHECK(TestAcceptDrops() == 0);
	CHECK(TestAcceptFailures() == 0);
	CHECK(TestSingleHumanConfirms() == 0);
	CHECK(TestTwoHumanAgreement() == 0);
	CHECK(TestPeerResolvedComparedOnResolve() == 0);
	CHECK(TestCompose() == 0);
	printf("native_match_select_session_test: all tests passed\n");
	return 0;
}
