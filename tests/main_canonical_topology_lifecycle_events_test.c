#include "MAIN/MainCanonicalTopologyLifecycleEvents.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression)                                                                                                                   \
	do                                                                                                                                  \
	{                                                                                                                                   \
		if (!(expression))                                                                                                                \
		{                                                                                                                               \
			fprintf(stderr, "%s:%d: check failed: %s\\n", __FILE__, __LINE__, #expression);                                      \
			return 1;                                                                                                                   \
		}                                                                                                                               \
	} while (0)

static struct MainCanonicalTopologyLifecycleEventV1 Event(uint32_t kind, uint32_t reason, uint64_t step)
{
	struct MainCanonicalTopologyLifecycleEventV1 event;
	event.kind = kind;
	event.reason = reason;
	event.opaqueSourceStep = step;
	return event;
}

static int TestPreservesObservedOrdering(void)
{
	struct MainCanonicalTopologyLifecycleEventsV1 recorder;
	const struct MainCanonicalTopologyLifecycleEventV1 events[] = {
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT, 7u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET, 8u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 9u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 10u},
	};

	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, events, 4u));
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	CHECK(recorder.count == 4u);
	CHECK(memcmp(recorder.events, events, sizeof(events)) == 0);
	for (uint32_t index = recorder.count; index < MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY; index++)
		CHECK(recorder.events[index].kind == 0 && recorder.events[index].reason == 0 &&
			recorder.events[index].opaqueSourceStep == 0);
	return 0;
}

static int TestAllReasonsAndKinds(void)
{
	struct MainCanonicalTopologyLifecycleEventsV1 recorder;
	const struct MainCanonicalTopologyLifecycleEventV1 events[] = {
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_CONSTRUCTED, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_TEST, 0u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP, UINT64_MAX},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_HUB_SWAP, 2u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE, 3u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_CHECKPOINT_RESTORE, 4u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 5u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_ARENA_RESET, 6u},
		{MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_GAME_TRACKER_ZERO, 7u},
	};

	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, events, (uint32_t)(sizeof(events) / sizeof(events[0]))));
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	return 0;
}

static int TestTransactionalRejection(void)
{
	struct MainCanonicalTopologyLifecycleEventsV1 recorder;
	struct MainCanonicalTopologyLifecycleEventsV1 before;
	struct MainCanonicalTopologyLifecycleEventV1 mixed[2];
	struct MainCanonicalTopologyLifecycleEventV1 valid =
		Event(MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 1u);
	struct MainCanonicalTopologyLifecycleEventV1 invalid = valid;

	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &valid, 1u));
	before = recorder;
	invalid.reason = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_COLD_BOOT;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &invalid, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	invalid = valid;
	invalid.kind = 0;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &invalid, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	mixed[0] = valid;
	mixed[1] = invalid;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, mixed, 2u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, NULL, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &valid, 0u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(NULL, &valid, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	return 0;
}

static int TestBatchCapacityAndStructuralValidation(void)
{
	struct MainCanonicalTopologyLifecycleEventsV1 recorder;
	struct MainCanonicalTopologyLifecycleEventsV1 before;
	struct MainCanonicalTopologyLifecycleEventV1 full[MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY];
	struct MainCanonicalTopologyLifecycleEventV1 one =
		Event(MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_POST_INIT, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 99u);

	for (uint32_t index = 0; index < MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY; index++)
		full[index] = Event(MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION,
			MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, index);
	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, full, MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY));
	before = recorder;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &one, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	recorder.events[recorder.count - 1u].reason = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_NONE;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &one, 1u));
	return 0;
}

static int TestZeroTailIsPartOfShape(void)
{
	struct MainCanonicalTopologyLifecycleEventsV1 recorder;
	struct MainCanonicalTopologyLifecycleEventsV1 before;
	const struct MainCanonicalTopologyLifecycleEventV1 event = {
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENT_MUTATION,
		MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_REASON_FULL_LOAD, 2u};

	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	CHECK(MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	recorder.events[0].opaqueSourceStep = 1u;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	before = recorder;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Append(&recorder, &event, 1u));
	CHECK(memcmp(&recorder, &before, sizeof(recorder)) == 0);
	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	recorder.version++;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	MainCanonicalTopologyLifecycleEventsV1_Init(&recorder);
	recorder.count = MAIN_CANONICAL_TOPOLOGY_LIFECYCLE_EVENTS_V1_CAPACITY + 1u;
	CHECK(!MainCanonicalTopologyLifecycleEventsV1_Validate(&recorder));
	return 0;
}

int main(void)
{
	if (TestPreservesObservedOrdering()) return 1;
	if (TestAllReasonsAndKinds()) return 1;
	if (TestTransactionalRejection()) return 1;
	if (TestBatchCapacityAndStructuralValidation()) return 1;
	if (TestZeroTailIsPartOfShape()) return 1;
	return 0;
}
